//-----------------------------------------------------------------------------
// Copyright (C) 2017 Merlok
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// EMV core functions
//-----------------------------------------------------------------------------

#include "emvcore.h"

// Got from here. Thanks)
// https://eftlab.co.uk/index.php/site-map/knowledge-base/211-emv-aid-rid-pix
static const char *PSElist [] = { 
	"325041592E5359532E4444463031", // 2PAY.SYS.DDF01 - Visa Proximity Payment System Environment - PPSE
	"315041592E5359532E4444463031"  // 1PAY.SYS.DDF01 - Visa Payment System Environment - PSE
};
static const size_t PSElistLen = sizeof(PSElist)/sizeof(char*);

typedef struct {
	enum CardPSVendor vendor;
	const char* aid;
} TAIDList;

static const TAIDList AIDlist [] = { 
	// Visa International
	{ CV_VISA, 	"A00000000305076010"},			// VISA ELO Credit	
	{ CV_VISA, 	"A0000000031010" },				// VISA Debit/Credit (Classic)	
	{ CV_VISA, 	"A000000003101001" },			// VISA Credit	
	{ CV_VISA, 	"A000000003101002" },			// VISA Debit	
	{ CV_VISA, 	"A0000000032010" },				// VISA Electron
	{ CV_VISA, 	"A0000000032020" },				// VISA	
	{ CV_VISA, 	"A0000000033010" },				// VISA Interlink	
	{ CV_VISA, 	"A0000000034010" },				// VISA Specific	
	{ CV_VISA, 	"A0000000035010" },				// VISA Specific	
	{ CV_VISA, 	"A0000000036010" },				// Domestic Visa Cash Stored Value	
	{ CV_VISA, 	"A0000000036020" },				// International Visa Cash Stored Value	
	{ CV_VISA, 	"A0000000038002" },				// VISA Auth, VisaRemAuthen EMV-CAP (DPA)	
	{ CV_VISA, 	"A0000000038010" },				// VISA Plus	
	{ CV_VISA, 	"A0000000039010" },				// VISA Loyalty	
	{ CV_VISA, 	"A000000003999910" },			// VISA Proprietary ATM	
	// Visa USA
	{ CV_VISA, 	"A000000098" },					// Debit Card
	{ CV_VISA, 	"A0000000980848" },				// Debit Card
	// Mastercard International
	{ CV_MASTERCARD, "A00000000401" },			// MasterCard PayPass	
	{ CV_MASTERCARD, "A0000000041010" },		// MasterCard Credit
	{ CV_MASTERCARD, "A00000000410101213" },	// MasterCard Credit
	{ CV_MASTERCARD, "A00000000410101215" },	// MasterCard Credit
	{ CV_MASTERCARD, "A0000000042010" },		// MasterCard Specific
	{ CV_MASTERCARD, "A0000000043010" },		// MasterCard Specific
	{ CV_MASTERCARD, "A0000000043060" },		// Maestro (Debit)
	{ CV_MASTERCARD, "A000000004306001" },		// Maestro (Debit)
	{ CV_MASTERCARD, "A0000000044010" },		// MasterCard Specific
	{ CV_MASTERCARD, "A0000000045010" },		// MasterCard Specific
	{ CV_MASTERCARD, "A0000000046000" },		// Cirrus
	{ CV_MASTERCARD, "A0000000048002" },		// SecureCode Auth EMV-CAP
	{ CV_MASTERCARD, "A0000000049999" },		// MasterCard PayPass	
	// American Express
	{ CV_AMERICANEXPRESS, "A000000025" },
	{ CV_AMERICANEXPRESS, "A0000000250000" },
	{ CV_AMERICANEXPRESS, "A00000002501" },
	{ CV_AMERICANEXPRESS, "A000000025010402" },
	{ CV_AMERICANEXPRESS, "A000000025010701" },
	{ CV_AMERICANEXPRESS, "A000000025010801" },
	// Groupement des Cartes Bancaires "CB"
	{ CV_CB, "A0000000421010" },				// Cartes Bancaire EMV Card	
	{ CV_CB, "A0000000422010" },		
	{ CV_CB, "A0000000423010" },		
	{ CV_CB, "A0000000424010" },		
	{ CV_CB, "A0000000425010" },		
	// JCB CO., LTD.
	{ CV_JCB, "A00000006510" },					// JCB	
	{ CV_JCB, "A0000000651010" },				// JCB J Smart Credit	
	// Other
	{ CV_OTHER, "A0000001544442" },				// Banricompras Debito - Banrisul - Banco do Estado do Rio Grande do SUL - S.A.
	{ CV_OTHER, "F0000000030001" },				// BRADESCO
	{ CV_OTHER, "A0000005241010" },				// RuPay - RuPay
	{ CV_OTHER, "D5780000021010" }				// Bankaxept - Bankaxept
};
static const size_t AIDlistLen = sizeof(AIDlist)/sizeof(TAIDList);

static bool APDULogging = false;
void SetAPDULogging(bool logging) {
	APDULogging = logging;
}

enum CardPSVendor GetCardPSVendor(uint8_t * AID, size_t AIDlen) {
	char buf[100] = {0};
	if (AIDlen < 1)
		return CV_NA;

	hex_to_buffer((uint8_t *)buf, AID, AIDlen, sizeof(buf) - 1, 0, 0, true);

	for(int i = 0; i < AIDlistLen; i ++) {
		if (strncmp(AIDlist[i].aid, buf, strlen(AIDlist[i].aid)) == 0){
			return AIDlist[i].vendor;
		}
	}	
	
	return CV_NA;
}

static bool print_cb(void *data, const struct tlv *tlv, int level, bool is_leaf) {
	emv_tag_dump(tlv, stdout, level);
	if (is_leaf) {
		dump_buffer(tlv->value, tlv->len, stdout, level);
	}

	return true;
}

void TLVPrintFromBuffer(uint8_t *data, int datalen) {
	struct tlvdb *t = NULL;
	t = tlvdb_parse_multi(data, datalen);
	if (t) {
		PrintAndLog("-------------------- TLV decoded --------------------");
		
		tlvdb_visit(t, print_cb, NULL, 0);
		tlvdb_free(t);
	} else {
		PrintAndLog("TLV ERROR: Can't parse response as TLV tree.");
	}
}

void TLVPrintFromTLVLev(struct tlvdb *tlv, int level) {
	if (!tlv) 
		return;
	
	tlvdb_visit(tlv, print_cb, NULL, level);
}

void TLVPrintFromTLV(struct tlvdb *tlv) {
	TLVPrintFromTLVLev(tlv, 0);
}

void TLVPrintAIDlistFromSelectTLV(struct tlvdb *tlv) {
	PrintAndLog("|------------------|--------|-------------------------|");
	PrintAndLog("|    AID           |Priority| Name                    |");
	PrintAndLog("|------------------|--------|-------------------------|");

	struct tlvdb *ttmp = tlvdb_find(tlv, 0x6f);
	if (!ttmp)
		PrintAndLog("|                         none                        |");
		
	while (ttmp) {
		const struct tlv *tgAID = tlvdb_get_inchild(ttmp, 0x84, NULL);
		const struct tlv *tgName = tlvdb_get_inchild(ttmp, 0x50, NULL);
		const struct tlv *tgPrio = tlvdb_get_inchild(ttmp, 0x87, NULL);
		if (!tgAID)
			break;
		PrintAndLog("|%s|   %s  |%s|", 
			sprint_hex_inrow_ex(tgAID->value, tgAID->len, 18), 
			(tgPrio) ? sprint_hex(tgPrio->value, 1) : "   ", 
			(tgName) ? sprint_ascii_ex(tgName->value, tgName->len, 25) : "                         ");
		
		ttmp = tlvdb_find_next(ttmp, 0x6f);
	}

	PrintAndLog("|------------------|--------|-------------------------|");
}

struct tlvdb *GetPANFromTrack2(const struct tlv *track2) {
	char track2Hex[200] = {0};
	uint8_t PAN[100] = {0};
	int PANlen = 0;
	char *tmp = track2Hex;

	if (!track2)
		return NULL;

	for (int i = 0; i < track2->len; ++i, tmp += 2)
		sprintf(tmp, "%02x", (unsigned int)track2->value[i]);
	
	int posD = strchr(track2Hex, 'd') - track2Hex;
	if (posD < 1)
		return NULL;
	
	track2Hex[posD] = 0;
	if (strlen(track2Hex) % 2) {
		track2Hex[posD] = 'F';
		track2Hex[posD + 1] = '\0';
	}
	
	param_gethex_to_eol(track2Hex, 0, PAN, sizeof(PAN), &PANlen);
	
  return tlvdb_fixed(0x5a, PANlen, PAN);
}

struct tlvdb *GetdCVVRawFromTrack2(const struct tlv *track2) {
	char track2Hex[200] = {0};
	char dCVVHex[100] = {0};
	uint8_t dCVV[100] = {0};
	int dCVVlen = 0;
	const int PINlen = 5; // must calculated from 9F67 MSD Offset but i have not seen this tag)
	char *tmp = track2Hex;
	
	if (!track2)
		return NULL;
	
	for (int i = 0; i < track2->len; ++i, tmp += 2)
		sprintf(tmp, "%02x", (unsigned int)track2->value[i]);
	
	int posD = strchr(track2Hex, 'd') - track2Hex;
	if (posD < 1)
		return NULL;

	memset(dCVVHex, '0', 32);
	// ATC
	memcpy(dCVVHex + 0, track2Hex + posD + PINlen + 11, 4);
	// PAN 5 hex
	memcpy(dCVVHex + 4, track2Hex, 5);
	// expire date
	memcpy(dCVVHex + 9, track2Hex + posD + 1, 4);
	// service code
	memcpy(dCVVHex + 13, track2Hex + posD + 5, 3);
	
	param_gethex_to_eol(dCVVHex, 0, dCVV, sizeof(dCVV), &dCVVlen);
	
  return tlvdb_fixed(0x02, dCVVlen, dCVV);
}

int EMVExchangeEx(bool ActivateField, bool LeaveFieldON, sAPDU apdu, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw, struct tlvdb *tlv) {
	uint8_t data[APDU_RES_LEN] = {0};

	*ResultLen = 0;
	if (sw)	*sw = 0;
	uint16_t isw = 0;
	
	if (ActivateField)
		DropField();
	
	// COMPUTE APDU
	memcpy(data, &apdu, 5);
	if (apdu.data)
		memcpy(&data[5], apdu.data, apdu.Lc);
	
	if (APDULogging)
		PrintAndLog(">>>> %s", sprint_hex(data, 6 + apdu.Lc));

	// 6 byes + data = INS + CLA + P1 + P2 + Lc + <data = Nc> + Le
	int res = ExchangeAPDU14a(data, 6 + apdu.Lc, ActivateField, LeaveFieldON, Result, (int)MaxResultLen, (int *)ResultLen);
	
	if (APDULogging)
		PrintAndLog("<<<< %s", sprint_hex(Result, *ResultLen));
	
	if (res) {
		return res;
	}

	*ResultLen -= 2;
	isw = Result[*ResultLen] * 0x0100 + Result[*ResultLen + 1];
	if (sw)
		*sw = isw;

	if (isw != 0x9000) {
		if (APDULogging)
			PrintAndLog("APDU(%02x%02x) ERROR: [%4X] %s", apdu.CLA, apdu.INS, isw, GetAPDUCodeDescription(*sw >> 8, *sw & 0xff));
		return 5;
	}

	// add to tlv tree
	if (tlv) {
		struct tlvdb *t = tlvdb_parse_multi(Result, *ResultLen);
		tlvdb_add(tlv, t);
	}
	
	return 0;
}

int EMVExchange(bool LeaveFieldON, sAPDU apdu, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw, struct tlvdb *tlv) {
	return EMVExchangeEx(false, LeaveFieldON, apdu, Result, MaxResultLen, ResultLen, sw, tlv);
}

int EMVSelect(bool ActivateField, bool LeaveFieldON, uint8_t *AID, size_t AIDLen, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw, struct tlvdb *tlv) {
	return EMVExchangeEx(ActivateField, LeaveFieldON, (sAPDU){0x00, 0xa4, 0x04, 0x00, AIDLen, AID}, Result, MaxResultLen, ResultLen, sw, tlv);
}

int EMVSelectPSE(bool ActivateField, bool LeaveFieldON, uint8_t PSENum, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw) {
	uint8_t buf[APDU_AID_LEN] = {0};
	*ResultLen = 0;
	int len = 0;
	int res = 0;
	switch (PSENum) {
		case 1:
			param_gethex_to_eol(PSElist[1], 0, buf, sizeof(buf), &len);
			break;
		case 2:
			param_gethex_to_eol(PSElist[0], 0, buf, sizeof(buf), &len);
			break;
		default:
			return -1;
	}
	
	// select
	res = EMVSelect(ActivateField, LeaveFieldON, buf, len, Result, MaxResultLen, ResultLen, sw, NULL);

	return res;
}

int EMVSearchPSE(bool ActivateField, bool LeaveFieldON, bool decodeTLV, struct tlvdb *tlv) {
	uint8_t data[APDU_RES_LEN] = {0};
	size_t datalen = 0;
	uint16_t sw = 0;
	int res;

	// select PPSE
	res = EMVSelectPSE(ActivateField, true, 2, data, sizeof(data), &datalen, &sw);

	if (!res){
		struct tlvdb *t = NULL;
		t = tlvdb_parse_multi(data, datalen);
		if (t) {
			int retrycnt = 0;
			struct tlvdb *ttmp = tlvdb_find_path(t, (tlv_tag_t[]){0x6f, 0xa5, 0xbf0c, 0x61, 0x00});
			if (!ttmp)
				PrintAndLog("PPSE don't have records.");
			
			while (ttmp) {
				const struct tlv *tgAID = tlvdb_get_inchild(ttmp, 0x4f, NULL);
				if (tgAID) {
					res = EMVSelect(false, true, (uint8_t *)tgAID->value, tgAID->len, data, sizeof(data), &datalen, &sw, tlv);

					// retry if error and not returned sw error
					if (res && res != 5) {
						if (++retrycnt < 3){
							continue;
						} else {
							// card select error, proxmark error
							if (res == 1) {
								PrintAndLog("Exit...");
								return 1;
							}
							
							retrycnt = 0;
							PrintAndLog("Retry failed [%s]. Skiped...", sprint_hex_inrow(tgAID->value, tgAID->len));
						}
						
						// next element
						ttmp = tlvdb_find_next(ttmp, 0x61);
						continue;
					}
					retrycnt = 0;

					// all is ok
					if (decodeTLV){
						PrintAndLog("%s:", sprint_hex_inrow(tgAID->value, tgAID->len));
						TLVPrintFromBuffer(data, datalen);
					}
				}
				
				ttmp = tlvdb_find_next(ttmp, 0x61);
			}

			tlvdb_free(t);
		} else {
			PrintAndLog("PPSE ERROR: Can't get TLV from response.");
		}		
	} else {
		PrintAndLog("PPSE ERROR: Can't select PPSE AID. Error: %d", res);
	}
	
	if(!LeaveFieldON)
		DropField();
	
	return res;
}

int EMVSearch(bool ActivateField, bool LeaveFieldON, bool decodeTLV, struct tlvdb *tlv) {
	uint8_t aidbuf[APDU_AID_LEN] = {0};
	int aidlen = 0;
	uint8_t data[APDU_RES_LEN] = {0};
	size_t datalen = 0;
	uint16_t sw = 0;
	
	int res = 0;
	int retrycnt = 0;
	for(int i = 0; i < AIDlistLen; i ++) {
		param_gethex_to_eol(AIDlist[i].aid, 0, aidbuf, sizeof(aidbuf), &aidlen);
		res = EMVSelect((i == 0) ? ActivateField : false, (i == AIDlistLen - 1) ? LeaveFieldON : true, aidbuf, aidlen, data, sizeof(data), &datalen, &sw, tlv);
		// retry if error and not returned sw error
		if (res && res != 5) {
			if (++retrycnt < 3){
				i--;
			} else {
				// card select error, proxmark error
				if (res == 1) {
					PrintAndLog("Exit...");
					return 1;
				}
				
				retrycnt = 0;
				PrintAndLog("Retry failed [%s]. Skiped...", AIDlist[i].aid);
			}
			continue;
		}
		retrycnt = 0;
		
		if (res)
			continue;
		
		if (decodeTLV){
			PrintAndLog("%s:", AIDlist[i].aid);
			TLVPrintFromBuffer(data, datalen);
		}
	}

	return 0;
}

int EMVSelectApplication(struct tlvdb *tlv, uint8_t *AID, size_t *AIDlen) {
	// check priority. 0x00 - highest
	int prio = 0xffff;
	
	*AIDlen = 0;

	struct tlvdb *ttmp = tlvdb_find(tlv, 0x6f);
	if (!ttmp)
		return 1;
		
	while (ttmp) {
		const struct tlv *tgAID = tlvdb_get_inchild(ttmp, 0x84, NULL);
		const struct tlv *tgPrio = tlvdb_get_inchild(ttmp, 0x87, NULL);
		
		if (!tgAID)
			break;

		if (tgPrio) {
			int pt = bytes_to_num((uint8_t*)tgPrio->value, (tgPrio->len < 2) ? tgPrio->len : 2); 
			if (pt < prio) {
				prio = pt;
				
				memcpy(AID, tgAID->value, tgAID->len);
				*AIDlen = tgAID->len;
			}
		} else {
			// takes the first application from list wo priority
			if (!*AIDlen) {
			memcpy(AID, tgAID->value, tgAID->len);
			*AIDlen = tgAID->len;				
			}
		}
		
		ttmp = tlvdb_find_next(ttmp, 0x6f);
	}
	
	return 0;
}

int EMVGPO(bool LeaveFieldON, uint8_t *PDOL, size_t PDOLLen, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw, struct tlvdb *tlv) {
	return EMVExchange(LeaveFieldON, (sAPDU){0x80, 0xa8, 0x00, 0x00, PDOLLen, PDOL}, Result, MaxResultLen, ResultLen, sw, tlv);
}

int EMVReadRecord(bool LeaveFieldON, uint8_t SFI, uint8_t SFIrec, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw, struct tlvdb *tlv) {
	return EMVExchange(LeaveFieldON, (sAPDU){0x00, 0xb2, SFIrec, (SFI << 3) | 0x04, 0, NULL}, Result, MaxResultLen, ResultLen, sw, tlv);
}

int EMVAC(bool LeaveFieldON, uint8_t RefControl, uint8_t *CDOL, size_t CDOLLen, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw, struct tlvdb *tlv) {
	return EMVExchange(LeaveFieldON, (sAPDU){0x80, 0xae, RefControl, 0x00, CDOLLen, CDOL}, Result, MaxResultLen, ResultLen, sw, tlv);
}

int EMVGenerateChallenge(bool LeaveFieldON, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw, struct tlvdb *tlv) {
	return EMVExchange(LeaveFieldON, (sAPDU){0x00, 0x84, 0x00, 0x00, 0x00, NULL}, Result, MaxResultLen, ResultLen, sw, tlv);
}

int MSCComputeCryptoChecksum(bool LeaveFieldON, uint8_t *UDOL, uint8_t UDOLlen, uint8_t *Result, size_t MaxResultLen, size_t *ResultLen, uint16_t *sw, struct tlvdb *tlv) {
	return EMVExchange(LeaveFieldON, (sAPDU){0x80, 0x2a, 0x8e, 0x80, UDOLlen, UDOL}, Result, MaxResultLen, ResultLen, sw, tlv);
}
