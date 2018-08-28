//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency HID commands (known)
//
// Useful resources:
// RF interface, programming a T55x7 clone, 26-bit HID H10301 encoding:
// http://www.proxmark.org/files/Documents/125%20kHz%20-%20HID/HID_format_example.pdf
//
// "Understanding Card Data Formats"
// https://www.hidglobal.com/sites/default/files/hid-understanding_card_data_formats-wp-en.pdf
//
// "What Format Do You Need?"
// https://www.hidglobal.com/sites/default/files/resource_files/hid-prox-br-en.pdf
//-----------------------------------------------------------------------------

#include "cmdlfhid.h"

#include <stdio.h>
#include <string.h>
#include "comms.h"
#include "ui.h"
#include "graph.h"
#include "cmdparser.h"
#include "cmddata.h"  //for g_debugMode, demodbuff cmds
#include "lfdemod.h" // for HIDdemodFSK
#include "hidcardformats.h"
#include "hidcardformatutils.h"
#include "util.h" // for param_get8,32,64


/**
 * Converts a hex string to component "hi2", "hi" and "lo" 32-bit integers, one nibble
 * at a time.
 *
 * Returns the number of nibbles (4 bits) entered.
 */
int hexstring_to_int96(/* out */ uint32_t* hi2,/* out */ uint32_t* hi, /* out */ uint32_t* lo, const char* str) {
  // TODO: Replace this with param_gethex when it supports arbitrary length
  // inputs.
  int n = 0, i = 0;

  while (sscanf(&str[i++], "%1x", &n ) == 1) {
    *hi2 = (*hi2 << 4) | (*hi >> 28);
    *hi = (*hi << 4) | (*lo >> 28);
    *lo = (*lo << 4) | (n & 0xf);
  }

  return i - 1;
}

//by marshmellow (based on existing demod + holiman's refactor)
//HID Prox demod - FSK RF/50 with preamble of 00011101 (then manchester encoded)
//print full HID Prox ID and some bit format details if found
int CmdFSKdemodHID(const char *Cmd)
{
  //raw fsk demod no manchester decoding no start bit finding just get binary from wave
  uint32_t hi2=0, hi=0, lo=0;

  uint8_t BitStream[MAX_GRAPH_TRACE_LEN]={0};
  size_t BitLen = getFromGraphBuf(BitStream);
  if (BitLen==0) return 0;
  //get binary from fsk wave
  int waveIdx = 0;
  int idx = HIDdemodFSK(BitStream,&BitLen,&hi2,&hi,&lo, &waveIdx);
  if (idx<0){
    if (g_debugMode){
      if (idx==-1){
        PrintAndLog("DEBUG: Just Noise Detected");
      } else if (idx == -2) {
        PrintAndLog("DEBUG: Error demoding fsk");
      } else if (idx == -3) {
        PrintAndLog("DEBUG: Preamble not found");
      } else if (idx == -4) {
        PrintAndLog("DEBUG: Error in Manchester data, SIZE: %d", BitLen);
      } else {
        PrintAndLog("DEBUG: Error demoding fsk %d", idx);
      }   
    }
    return 0;
  }
  if (hi2==0 && hi==0 && lo==0) {
    if (g_debugMode) PrintAndLog("DEBUG: Error - no values found");
    return 0;
  }
  
  if (hi2 != 0)
    PrintAndLog("HID Prox TAG ID: %x%08x%08x",
      (unsigned int) hi2, (unsigned int) hi, (unsigned int) lo
    );
  else
    PrintAndLog("HID Prox TAG ID: %x%08x",
      (unsigned int) hi, (unsigned int) lo
    );

  hidproxmessage_t packed = initialize_proxmessage_object(hi2, hi, lo);
  bool ret = HIDTryUnpack(&packed, false);

  if (!ret) {
    PrintAndLog("Invalid or unsupported tag length.");
  }
  setDemodBuf(BitStream,BitLen,idx);
  setClockGrid(50, waveIdx + (idx*50));
  if (g_debugMode){ 
    PrintAndLog("DEBUG: idx: %d, Len: %d, Printing Demod Buffer:", idx, BitLen);
    printDemodBuff();
  }
  return 1;
}

int CmdHIDReadFSK(const char *Cmd)
{
  int findone=0;
  if(Cmd[0]=='1') findone=1;
  UsbCommand c={CMD_HID_DEMOD_FSK};
  c.arg[0]=findone;
  SendCommand(&c);
  return 0;
}

int CmdHIDSim(const char *Cmd)
{
  uint32_t hi2 = 0, hi = 0, lo = 0;
  hexstring_to_int96(&hi2, &hi, &lo, Cmd);
  if (hi >= 0x40 || hi2 != 0) {
    PrintAndLog("This looks like a long tag ID. Use 'lf simfsk' for long tags. Aborting!");
    return 0;
  }

  PrintAndLog("Emulating tag with ID %x%08x", hi, lo);
  PrintAndLog("Press pm3-button to abort simulation");

  UsbCommand c = {CMD_HID_SIM_TAG, {hi, lo, 0}};
  SendCommand(&c);
  return 0;
}

int CmdHIDClone(const char *Cmd)
{
  unsigned int hi2 = 0, hi = 0, lo = 0;
  UsbCommand c;
  hexstring_to_int96(&hi2, &hi, &lo, Cmd);
    
  if (hi >= 0x40 || hi2 != 0) {
    PrintAndLog("Cloning tag with long ID %x%08x%08x", hi2, hi, lo);
    c.d.asBytes[0] = 1;
  } else {
    PrintAndLog("Cloning tag with ID %x%08x", hi, lo);
    c.d.asBytes[0] = 0;
  }

  c.cmd = CMD_HID_CLONE_TAG;
  c.arg[0] = (hi2 & 0x000FFFFF);
  c.arg[1] = hi;
  c.arg[2] = lo;

  SendCommand(&c);
  return 0;
}

int CmdHIDDecode(const char *Cmd){
  if (strlen(Cmd)<3) {
    PrintAndLog("Usage:  lf hid decode <id> {p}");
    PrintAndLog("        (optional) p: Ignore invalid parity");
    PrintAndLog("        sample: lf hid decode 2006f623ae");
    return 0;
  }

  uint32_t top = 0, mid = 0, bot = 0;
  bool ignoreParity = false;
  hexstring_to_int96(&top, &mid, &bot, Cmd);
  hidproxmessage_t packed = initialize_proxmessage_object(top, mid, bot);

  char opt = param_getchar(Cmd, 1);
  if (opt == 'p') ignoreParity = true;

  HIDTryUnpack(&packed, ignoreParity);
  return 0;
}
int CmdHIDEncode(const char *Cmd) {
  if (strlen(Cmd) == 0) {
    PrintAndLog("Usage:  lf hid encode <format> <facility code (decimal)> <card number (decimal)> [issue level (decimal)]");
    PrintAndLog("        sample: lf hid encode H10301 123 4567");
    return 0;
  }

  int formatIndex = -1;
  if (!strcmp(Cmd, "help") || !strcmp(Cmd, "h") || !strcmp(Cmd, "list") || !strcmp(Cmd, "?")){
    HIDListFormats();
    return 0;
  } else {
    char format[16];
    memset(format, 0, sizeof(format));
    param_getstr(Cmd, 0, format, sizeof(format));
    formatIndex = HIDFindCardFormat(format);
    if (formatIndex == -1) {
      HIDListFormats();
      return 0;
    }
  }

  hidproxcard_t card;
  memset(&card, 0, sizeof(hidproxcard_t));
  card.FacilityCode = param_get32ex(Cmd, 1, 0, 10);
  card.CardNumber = param_get64ex(Cmd, 2, 0, 10);
  card.IssueLevel = param_get32ex(Cmd, 3, 0, 10);
  card.ParitySupported = true; // Try to encode parity if supported.

  hidproxmessage_t packed;
  memset(&packed, 0, sizeof(hidproxmessage_t));
  if (HIDPack(formatIndex, &card, &packed)){
    if (packed.top != 0) {
      PrintAndLog("HID Prox TAG ID: %x%08x%08x",
        (unsigned int)packed.top, (unsigned int)packed.mid, (unsigned int)packed.bot);
    } else {
      PrintAndLog("HID Prox TAG ID: %x%08x",
        (unsigned int)packed.mid, (unsigned int)packed.bot); 
    }
  } else {
    PrintAndLog("The provided data could not be encoded with the selected format.");
  }
  return 0;
}

int CmdHIDWrite(const char *Cmd) {
  if (strlen(Cmd) == 0) {
    PrintAndLog("Usage:  lf hid write <format> <facility code (decimal)> <card number (decimal)> [issue level (decimal)]");
    PrintAndLog("        sample: lf hid write H10301 123 4567");
    return 0;
  }

  int formatIndex = -1;
  if (!strcmp(Cmd, "help") || !strcmp(Cmd, "h") || !strcmp(Cmd, "list") || !strcmp(Cmd, "?")){
    HIDListFormats();
    return 0;
  } else {
    char format[16];
    memset(format, 0, sizeof(format));
    param_getstr(Cmd, 0, format, sizeof(format));
    formatIndex = HIDFindCardFormat(format);
    if (formatIndex == -1) {
      HIDListFormats();
      return 0;
    }
  }

  hidproxcard_t card;
  memset(&card, 0, sizeof(hidproxcard_t));
  card.FacilityCode = param_get32ex(Cmd, 1, 0, 10);
  card.CardNumber = param_get64ex(Cmd, 2, 0, 10);
  card.IssueLevel = param_get32ex(Cmd, 3, 0, 10);
  card.ParitySupported = true; // Try to encode parity if supported.

  hidproxmessage_t packed;
  memset(&packed, 0, sizeof(hidproxmessage_t));
  if (HIDPack(formatIndex, &card, &packed)){
    UsbCommand c;
    if (packed.top != 0) {
      PrintAndLog("HID Prox TAG ID: %x%08x%08x",
        (unsigned int)packed.top, (unsigned int)packed.mid, (unsigned int)packed.bot);
      c.d.asBytes[0] = 1;
    } else {
      PrintAndLog("HID Prox TAG ID: %x%08x",
        (unsigned int)packed.mid, (unsigned int)packed.bot); 
      c.d.asBytes[0] = 0;
    }

    c.cmd = CMD_HID_CLONE_TAG;
    c.arg[0] = (packed.top & 0x000FFFFF);
    c.arg[1] = packed.mid;
    c.arg[2] = packed.bot;
    SendCommand(&c);

  } else {
    PrintAndLog("The provided data could not be encoded with the selected format.");
  }
  return 0;
}

static int CmdHelp(const char *Cmd); // define this now so the below won't error out.
static command_t CommandTable[] = 
{
  {"help",      CmdHelp,        1, "This help"},
  {"demod",     CmdFSKdemodHID, 1, "Demodulate HID Prox from GraphBuffer"},
  {"read",      CmdHIDReadFSK,  0, "['1'] Realtime HID FSK Read from antenna (option '1' for one tag only)"},
  {"sim",       CmdHIDSim,      0, "<ID> -- HID tag simulator"},
  {"clone",     CmdHIDClone,    0, "<ID> -- Clone HID to T55x7 (tag must be in antenna)"},
  {"decode",    CmdHIDDecode,   1, "<ID> -- Try to decode an HID tag and show its contents"},
  {"encode",    CmdHIDEncode,   1, "<format> <fc> <num> -- Encode an HID ID with the specified format, facility code and card number"},
  {"write",     CmdHIDWrite,    0, "<format> <fc> <num> -- Encode and write to a T55x7 tag (tag must be in antenna)"},
  {NULL, NULL, 0, NULL}
};

int CmdLFHID(const char *Cmd)
{
  CmdsParse(CommandTable, Cmd);
  return 0;
}

int CmdHelp(const char *Cmd)
{
  CmdsHelp(CommandTable);
  return 0;
}
