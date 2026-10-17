#include "stdafx.h"
#include "RFXBase.h"
#include "../main/mainworker.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "../main/RFXtrx.h"

static const char* RFXFirmwareTypeName(uint8_t type)
{
    switch (type)
    {
    case FWtyperec:     return "rec";
    case FWtype1:       return "1";
    case FWtype2:       return "2";
    case FWtypeExt:     return "Ext";
    case FWtypeExt2:    return "Ext2";
    case FWtypePro1:    return "Pro1";
    case FWtypePro2:    return "Pro2";
    case FWtypeProXL1:  return "Pro XL1";
    case FWtypeProXL2:  return "Pro XL2";
    case FWtypeRFX433:  return "RFX433 (RFM69 433)";
    case FWtypeRFX868:  return "RFX868 (RFM69 868)";
    case FWtypeProXL95: return "Pro XL95";
    case FWtypeRFX433XXL: return "RFX433XXL";
    case FWtypeRFX310:  return "RFX310";
    default:
        static char buf[16];
        snprintf(buf, sizeof(buf), "0x%02X", type);
        return buf;
    }
}

CRFXTeleinfoHelper::CRFXTeleinfoHelper()
{
	InitTeleinfo();
	m_iRateLimit = 2;
	m_bDisableCRC = false;
}

void CRFXTeleinfoHelper::SetHwdID(int HwdID)
{
	m_HwdID = HwdID;
}

CRFXBase::CRFXBase()
{
	m_NoiseLevel = 0;
	m_AsyncType = ATYPE_DISABLED;
}

bool CRFXBase::onInternalMessage(const unsigned char *pBuffer, const size_t Len, const bool checkValid/* = true*/)
{
	if (!m_bEnableReceive)
		return true; //receiving not enabled

	size_t ii = 0;
	while (ii < Len)
	{
		if (m_rxbufferpos == 0)	//1st char of a packet received
		{
			if (pBuffer[ii] == 0) //ignore first char if 00
				return true;
		}
		m_rxbuffer[m_rxbufferpos] = pBuffer[ii];
		m_rxbufferpos++;
		if (m_rxbufferpos >= sizeof(m_rxbuffer) - 1)
		{
			//something is out of sync here!!
			//restart
			Log(LOG_ERROR, "input buffer out of sync, going to restart!....");
			m_rxbufferpos = 0;
			return false;
		}
		if (m_rxbufferpos > m_rxbuffer[0])
		{
			if (!checkValid || CheckValidRFXData((uint8_t*)&m_rxbuffer))
			{
				const uint8_t* pData = reinterpret_cast<const uint8_t*>(m_rxbuffer);
			    uint8_t pLen = pData[0];
				uint8_t pType = pData[1];
				uint8_t pSubtype = pData[2];
				uint8_t pSeq = pData[3];
				uint8_t pCmd = pData[4];
				uint8_t pType = pData[5];
				uint8_t pFWversion = pData[6];

				// hex dump
				std::ostringstream oss;
				oss << std::hex << std::setfill('0');
				for (size_t i = 0; i <= pLen && i < sizeof(m_rxbuffer); ++i) {
					oss << std::setw(2) << static_cast<int>(pData[i]);
					if (i != pLen) oss << ' ';
				}
				std::string hex = oss.str();

				// printable ASCII (non-printables -> '.')
				std::string ascii;
				for (size_t i = 2; i <= pLen && i < sizeof(m_rxbuffer); ++i) {
					unsigned char c = static_cast<unsigned char>(pData[i]);
					ascii.push_back(std::isprint(c) ? static_cast<char>(c) : '.');
				}

				// compute FW type only for Interface Response packets and when packet is large enough
				std::string fwTypeStr;
				if ((pType == pTypeInterfaceMessage) && (pSubtype == sTypeInterfaceResponse) && (pLen > 14)) {
					uint8_t pFwType = pData[14];
					fwTypeStr = RFXFirmwareTypeName(pFwType);
				} else if ((pType == pTypeInterfaceMessage) && (pLen > 14)) {
					// For other interface-message subtypes, the byte at index 14 may be ASCII or other data;
					// show raw hex and ASCII to help diagnosis instead of trying to map it.
					char tmp[16];
					snprintf(tmp, sizeof(tmp), "0x%02X", static_cast<unsigned>(pData[14]));
					fwTypeStr = std::string("[raw: ") + tmp + "] (not an InterfaceResponse)";
				} else {
					fwTypeStr = "(n/a)";
				}

				// compute firmware version (major.minor) with preferred explicit fields (msg7/msg8)
				// and fallback to packed msg2 (high nibble = major, low nibble = minor)
				std::string fwVersionStr;
				if ((pType == pTypeInterfaceMessage) && (pSubtype == sTypeInterfaceResponse) && (pLen > 12)) {
					// IRESPONSE struct: msg7 is at pData[11], msg8 at pData[12]
					uint8_t major = pData[11];
					uint8_t minor = pData[12];
					std::ostringstream voss;
					voss << static_cast<unsigned>(major) << "." << static_cast<unsigned>(minor);
					fwVersionStr = voss.str();
				}
				else if (pLen > 6) {
					// fallback: parse msg2 (pData[6]) as packed nibble: high = major, low = minor
					uint8_t packed = pData[6];
					uint8_t major = (packed >> 4) & 0x0F;
					uint8_t minor = packed & 0x0F;
					// format minor as decimal (e.g. 6.15) - if you prefer hex for minor use std::hex
					std::ostringstream voss;
					voss << static_cast<unsigned>(major) << "." << static_cast<unsigned>(minor);
					fwVersionStr = voss.str();
				}
				else {
					fwVersionStr = "(n/a)";
				}

				_log.Debug(DEBUG_HARDWARE,
					"RFX pkt len=%u type=0x%02X subtype=0x%02X seq=0x%02X cmd=0x%02X trx=0x%02X fwver=0x%02X fwtype=%s fwVersion=%s hex=\"%s\" ascii=\"%s\"",
					static_cast<unsigned>(pLen),
					static_cast<unsigned>(pType),
					static_cast<unsigned>(pSubtype),
					static_cast<unsigned>(pSeq),
					static_cast<unsigned>(pCmd),
					static_cast<unsigned>(pType),
					static_cast<unsigned>(pFWversion),
					fwTypeStr.c_str(),
					fwVersionStr.c_str(),
					hex.c_str(),
					ascii.c_str());

				sDecodeRXMessage(this, (const uint8_t *)&m_rxbuffer, nullptr, -1, m_Name.c_str());
			}
			else
				Log(LOG_ERROR, "Invalid data received!....");

			m_rxbufferpos = 0;    //set to zero to receive next message
		}
		ii++;
	}
	return true;
}

bool CRFXBase::CheckValidRFXData(const uint8_t *pData)
{
	uint8_t pLen = pData[0];
	uint8_t pType = pData[1];
	uint8_t pSubtype = pData[2];
	uint8_t pSeq = pData[3];
	uint8_t pCmd = pData[4];

	uint8_t pTrxType = pData[5];
	uint8_t pFwv = pData[6];
	uint8_t p7 = pData[7];
	uint8_t p8 = pData[8];
	uint8_t p9 = pData[9];
	uint8_t p10 = pData[10];
	uint8_t p11 = pData[11];
	uint8_t p12 = pData[12];
	uint8_t p13 = pData[13];
	uint8_t pFwType = (pLen > 14) ? pData[14] : 0;

	_log.Debug(DEBUG_HARDWARE,
    	"Debug ValidRFXData len=%u type=0x%02X subtype=0x%02X seq=0x%02X cmd=0x%02X trx=0x%02X fwver=0x%02X fwtype=%s",
    	static_cast<unsigned>(pLen),
    	static_cast<unsigned>(pType),
    	static_cast<unsigned>(pSubtype),
    	static_cast<unsigned>(pSeq),
    	static_cast<unsigned>(pCmd),
    	static_cast<unsigned>(pTrxType),
    	static_cast<unsigned>(pFwv),
    	RFXFirmwareTypeName(pFwType));

	if (pLen < 1)
		return false;
	switch (pType)
	{
	case pTypeInterfaceControl:
		return (pLen == 0x0D);
	case pTypeInterfaceMessage:
		return (pLen == 0x14);
	case pTypeRecXmitMessage:
		return (pLen == 0x04);
	case pTypeUndecoded:
		return (pLen > 2);
	case pTypeLighting1:
		return (pLen == 0x07);
	case pTypeLighting2:
		return (pLen == 0x0B);
	case pTypeLighting3:
		return (pLen == 0x08);
	case pTypeLighting4:
		return (pLen == 0x09);
	case pTypeLighting5:
		return (pLen == 0x0A);
	case pTypeLighting6:
		return (pLen == 0x0B);
	case pTypeChime:
		return (pLen >= 0x07);
	case pTypeFan:
		return (pLen == 0x08 || pLen == 0x11 || pLen == 0x31);
	case pTypeCurtain:
		return (pLen == 0x07);
	case pTypeBlinds:
		return (pLen == 0x09);
	case pTypeRFY:
		return (pLen == 0x0C);
	case pTypeHomeConfort:
		return (pLen == 0x0C);
	case pTypeSecurity1:
		return (pLen == 0x08);
	case pTypeSecurity2:
		return (pLen == 0x1C);
	case pTypeCamera:
		return (pLen == 0x06);
	case pTypeRemote:
		return (pLen == 0x06);
	case pTypeThermostat1:
		return (pLen == 0x09);
	case pTypeThermostat2:
		return (pLen == 0x06);
	case pTypeThermostat3:
		return (pLen == 0x08);
	case pTypeThermostat4:
		return (pLen == 0x0C);
	case pTypeRadiator1:
		return (pLen == 0x0C);
	case pTypeBBQ:
		return (pLen == 0x0A);
	case pTypeTEMP_RAIN:
		return (pLen == 0x0A);
	case pTypeTEMP:
		return (pLen == 0x08);
	case pTypeHUM:
		return (pLen == 0x08);
	case pTypeTEMP_HUM:
		return (pLen == 0x0A);
	case pTypeBARO:
		return (pLen == 0x09);
	case pTypeTEMP_BARO:
		return (pLen == 0x10);
	case pTypeTEMP_HUM_BARO:
		return (pLen == 0x0D);
	case pTypeRAIN:
		return (pLen == 0x0B);
	case pTypeWIND:
		return (pLen == 0x10);
	case pTypeUV:
		return (pLen == 0x09);
	case pTypeDT:
		return (pLen == 0x0D);
	case pTypeCURRENT:
		return (pLen == 0x0D);
	case pTypeENERGY:
		return (pLen == 0x11);
	case pTypeCURRENTENERGY:
		return (pLen == 0x13);
	case pTypePOWER:
		return (pLen == 0x0F);
	case pTypeWEIGHT:
		return (pLen == 0x08);
	case pTypeCARTELECTRONIC:
		return (pLen == 0x15 || pLen == 0x11);
	case pTypeRFXSensor:
		return (pLen == 0x07);
	case pTypeRFXMeter:
		return (pLen == 0x0A);
	case pTypeFS20:
		return (pLen == 0x09);
	case pTypeASYNCPORT:
		return (pLen == 0x0B);
	case pTypeASYNCDATA:
		return (pLen > 0x03);
	case pTypeWEATHER:
		return (pLen == 0x1F);
	case pTypeSOLAR:
		return (pLen == 0x0A);
	case pTypeHunter:
		return (pLen == 0x0B);
	case pTypeLEVELSENSOR:
		return (pLen == 0x0D);
	case pTypeLIGHTNING:
		return (pLen == 0x0C);
	case pTypeDDxxxx:
		return (pLen == 0x0C);
	case pTypeHoneywell_AL:
		return (pLen == 0x0A);
	default:
		return false;//unknown Type
	}
	return false;
}

void CRFXBase::SetAsyncType(_eRFXAsyncType const AsyncType)
{
	m_AsyncType = AsyncType;
	Set_Async_Parameters(m_AsyncType);
	if (AsyncType == ATYPE_TELEINFO_1200 || AsyncType == ATYPE_TELEINFO_19200)
		m_teleinfoHelper.SetHwdID(m_HwdID);
}

void CRFXBase::Set_Async_Parameters(const _eRFXAsyncType AsyncType)
{
	tRBUF cmd;
	cmd.ASYNCPORT.packetlength = sizeof(cmd.ASYNCPORT) - 1;
	cmd.ASYNCPORT.packettype = pTypeASYNCPORT;
	cmd.ASYNCPORT.subtype = sTypeASYNCconfig;
	cmd.ASYNCPORT.seqnbr = m_SeqNr++;

	uint8_t baudrate = 0, parity = 0, databits = 0, stopbits = 0, polarity = 0;
	uint8_t cmnd = asyncdisable;

	switch (AsyncType)
	{
	case ATYPE_P1_DSMR_1:
	case ATYPE_P1_DSMR_2:
	case ATYPE_P1_DSMR_3:
		baudrate = asyncbaud9600;
		parity = asyncParityEven;
		databits = asyncDatabits7;
		stopbits = asyncStopbits1;
		polarity = asyncPolarityInvers;
		cmnd = asyncreceiveP1;
		Log(LOG_STATUS, "Async mode set to: 'DSMR 1/2/3'");
		break;
	case ATYPE_P1_DSMR_4:
	case ATYPE_P1_DSMR_5:
		baudrate = asyncbaud115200;
		parity = asyncParityNo;
		databits = asyncDatabits8;
		stopbits = asyncStopbits1;
		polarity = asyncPolarityInvers;
		cmnd = asyncreceiveP1;
		Log(LOG_STATUS, "Async mode set to: 'DSMR 4/5'");
		break;
	case ATYPE_TELEINFO_1200:
		baudrate = asyncbaud1200;
		parity = asyncParityEven;
		databits = asyncDatabits7;
		stopbits = asyncStopbits1;
		polarity = asyncPolarityInvers;
		cmnd = asyncreceiveTeleinfo;
		Log(LOG_STATUS, "Async mode set to: 'Teleinfo 1200'");
		break;
	case ATYPE_TELEINFO_19200:
		baudrate = asyncbaud19200;
		parity = asyncParityEven;
		databits = asyncDatabits7;
		stopbits = asyncStopbits1;
		polarity = asyncPolarityInvers;
		cmnd = asyncreceiveTeleinfo;
		Log(LOG_STATUS, "Async mode set to: 'Teleinfo 19200'");
		break;
	case ATYPE_DISABLED:
		Log(LOG_STATUS, "Async Disabled");
		break;
	default:
		Log(LOG_ERROR, "unknown ASync type received!....");
		return; //not handled yet!
	}

	cmd.ASYNCPORT.cmnd = cmnd;
	cmd.ASYNCPORT.baudrate = baudrate;
	cmd.ASYNCPORT.parity = parity;
	cmd.ASYNCPORT.databits = databits;
	cmd.ASYNCPORT.stopbits = stopbits;
	cmd.ASYNCPORT.polarity = polarity;
	cmd.ASYNCPORT.filler1 = 0;
	cmd.ASYNCPORT.filler2 = 0;

	WriteToHardware((const char*)&cmd, sizeof(cmd.ASYNCPORT));
}

void CRFXBase::Parse_Async_Data(const uint8_t *pData, const int Len)
{
	switch (m_AsyncType)
	{
	case ATYPE_P1_DSMR_1:
	case ATYPE_P1_DSMR_2:
	case ATYPE_P1_DSMR_3:
	case ATYPE_P1_DSMR_4:
	case ATYPE_P1_DSMR_5:
		ParseP1Data(pData, Len, false, 0);
		break;
	case ATYPE_TELEINFO_1200:
	case ATYPE_TELEINFO_19200:
		m_teleinfoHelper.ProcessData(reinterpret_cast<const char*>(pData), Len);
		break;
	default:
		Log(LOG_ERROR, "Parse_Async_Data: unknown async type %d", m_AsyncType);
		break;
	}
}

void CRFXBase::SendCommand(const unsigned char Cmd)
{
	tRBUF cmd;
	cmd.ICMND.packetlength = 13;
	cmd.ICMND.packettype = 0;
	cmd.ICMND.subtype = 0;
	cmd.ICMND.seqnbr = m_SeqNr++;
	cmd.ICMND.cmnd = Cmd;
	cmd.ICMND.freqsel = 0;
	cmd.ICMND.xmitpwr = 0;
	cmd.ICMND.msg3 = 0;
	cmd.ICMND.msg4 = 0;
	cmd.ICMND.msg5 = 0;
	cmd.ICMND.msg6 = 0;
	cmd.ICMND.msg7 = 0;
	cmd.ICMND.msg8 = 0;
	cmd.ICMND.msg9 = 0;
	WriteToHardware((const char*)&cmd, sizeof(cmd.ICMND));
}

bool CRFXBase::SetRFXCOMHardwaremodes(const unsigned char Mode1, const unsigned char Mode2, const unsigned char Mode3, const unsigned char Mode4, const unsigned char Mode5, const unsigned char Mode6)
{
	tRBUF Response;
	Response.ICMND.packetlength = sizeof(Response.ICMND) - 1;
	Response.ICMND.packettype = pTypeInterfaceControl;
	Response.ICMND.subtype = sTypeInterfaceCommand;
	Response.ICMND.seqnbr = m_SeqNr++;
	Response.ICMND.cmnd = cmdSETMODE;
	Response.ICMND.freqsel = Mode1;
	Response.ICMND.xmitpwr = Mode2;
	Response.ICMND.msg3 = Mode3;
	Response.ICMND.msg4 = Mode4;
	Response.ICMND.msg5 = Mode5;
	Response.ICMND.msg6 = Mode6;
	if (!WriteToHardware((const char*)&Response, sizeof(Response.ICMND)))
		return false;
	m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&Response, nullptr, -1, m_Name.c_str());
	//Save it also
	SendCommand(cmdSAVE);

	m_rxbufferpos = 0;

	return true;
}

void CRFXBase::SendResetCommand()
{
	std::lock_guard<std::mutex> l(readQueueMutex);
	m_bEnableReceive = false;
	m_rxbufferpos = 0;
	//Send Reset
	SendCommand(cmdRESET);
	//wait at least 500ms
	sleep_milliseconds(500);
	m_rxbufferpos = 0;
	m_bEnableReceive = true;

	SendCommand(cmdStartRec);
	sleep_milliseconds(50);
	SendCommand(cmdSTATUS);
}

