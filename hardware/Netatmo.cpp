#include "stdafx.h"
#include "Netatmo.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "../main/SQLHelper.h"
#include "../main/RFXtrx.h"
#include "hardwaretypes.h"
#include "../httpclient/HTTPClient.h"
#include "../main/json_helper.h"

#define round(a) ( int ) ( a + .5 )

#define NETATMO_OAUTH2_TOKEN_URI "https://api.netatmo.net/oauth2/token"
#define NETATMO_API_URI "https://api.netatmo.com/"
#define NETATMO_SCOPES "read_station read_smarther write_smarther read_thermostat write_thermostat read_camera write_camera read_doorbell read_presence write_presence read_homecoach read_carbonmonoxidedetector read_smokedetector"
#define NETATMO_REDIRECT_URI "http://localhost/netatmo"
// https://api.netatmo.com/oauth2/authorize?client_id=<CLIENT_ID>&redirect_uri=http://localhost/netatmo&state=teststate&scope=read_station%20read_smarther%20write_smarther%20read_thermostat%20write_thermostat%20read_camera%20write_camera%20read_doorbell%20read_presence%20write_presence%20read_homecoach%20read_carbonmonoxidedetector%20read_smokedetector

#ifdef _DEBUG
//#define DEBUG_NetatmoWeatherStationR
#endif

#ifdef DEBUG_NetatmoWeatherStationW
void SaveString2Disk(std::string str, std::string filename)
{
	FILE* fOut = fopen(filename.c_str(), "wb+");
	if (fOut)
	{
		fwrite(str.c_str(), 1, str.size(), fOut);
		fclose(fOut);
	}
}
#endif

#ifdef DEBUG_NetatmoWeatherStationR
std::string ReadFile(std::string filename)
{
	std::ifstream file;
	std::string sResult = "";
	file.open(filename.c_str());
	if (!file.is_open())
		return "";
	std::string sLine;
	while (!file.eof())
	{
		getline(file, sLine);
		sResult += sLine;
	}
	file.close();
	return sResult;
}
#endif

struct _tNetatmoDevice
{
	std::string ID;
	std::string ModuleName;
	std::string StationName;               // This is not used ?
	std::vector<std::string> ModulesIDs;
	//Json::Value Modules;
};

CNetatmo::CNetatmo(const int ID, const std::string& username, const std::string& password)
	: m_username(CURLEncode::URLDecode(username))
	, m_password(CURLEncode::URLDecode(password))
{
	m_scopes = NETATMO_SCOPES;
	m_redirectUri = NETATMO_REDIRECT_URI;
	m_authCode = m_password;

	size_t pos = m_username.find(":");
	if (pos != std::string::npos)
	{
		m_clientId = m_username.substr(0, pos);
		m_clientSecret = m_username.substr(pos + 1);
	}
	else
	{
		Log(LOG_ERROR, "The username does not contain the client_id:client_secret!");
		Debug(DEBUG_HARDWARE,"The username does not contain the client_id:client_secret! (%s)", m_username.c_str());
	}

	m_nextRefreshTs = mytime(nullptr);
	m_isLogged = false;

        m_weatherType = NETYPE_WEATHER_STATION;
        m_homecoachType = NETYPE_AIRCARE;
        m_energyType = NETYPE_ENERGY;

	m_HwdID = ID;
	m_Home_ID = "";

	m_ActHome = 0;

	m_bPollThermostat = true;
	m_bPollWeatherData = true;
	m_bPollHomecoachData = true;
	m_bPollHomeStatus = true;
	//m_bPollco2Data = true;
	m_bFirstTimeThermostat = true;
	m_bFirstTimeWeatherData = true;
	m_tSetpointUpdateTime = time(nullptr);
	
	Init();
}

void CNetatmo::Init()
{
	m_RainOffset.clear();
	m_OldRainCounter.clear();
	m_RoomNames.clear();
	m_RoomIDs.clear();
	m_ModuleNames.clear();
	m_ModuleIDs.clear();
	m_Room_Type.clear();
        m_Module_category.clear();
	m_thermostatDeviceID.clear();
	m_thermostatModuleID.clear();
        m_Camera_Name.clear();
        m_Camera_ID.clear();
        m_Smoke_Name.clear();
        m_Smoke_ID.clear();;
        m_Persons.clear();
        m_PersonsNames.clear();
        m_PersonsIDs.clear();
        m_ScheduleNames.clear();
        m_ScheduleIDs.clear();
        m_ZoneNames.clear();
        m_ZoneIDs.clear();
	m_bPollThermostat = true;
	m_bPollWeatherData = true;
	m_bPollHomecoachData = true;
	m_bPollHomeStatus = true;
	//m_bPollco2Data = true;
	m_bFirstTimeThermostat = true;
	m_bFirstTimeWeatherData = true;
	m_bForceSetpointUpdate = false;

        m_energyType = NETYPE_ENERGY;
        m_weatherType = NETYPE_WEATHER_STATION;
        m_homecoachType = NETYPE_AIRCARE;

	m_bForceLogin = false;
}

bool CNetatmo::StartHardware()
{
	RequestStart();

	Init();
	//Start worker thread
	m_thread = std::make_shared<std::thread>([this] { Do_Work(); });
	SetThreadNameInt(m_thread->native_handle());
	m_bIsStarted = true;
	sOnConnected(this);
	return (m_thread != nullptr);
}

bool CNetatmo::StopHardware()
{
	if (m_thread)
	{
		RequestStop();
		m_thread->join();
		m_thread.reset();
	}
	m_bIsStarted = false;
	return true;
}

void CNetatmo::Do_Work()
{
	int sec_counter = 600 - 5;
	bool bFirstTimeWS = true;
	bool bFirstTimeHS = true;
	bool bFirstTimeSS = true;
	bool bFirstTimeCS = true;
	bool bFirstTimeTH = true;
	Log(LOG_STATUS, "Worker started...");
	while (!IsStopRequested(1000))
	{
		sec_counter++;
		if (sec_counter % 12 == 0) {
			m_LastHeartbeat = mytime(nullptr);
		}

		if (!m_isLogged)
		{
			if (sec_counter % 30 == 0)
			{
				Login();
			}
		}
		if (m_isLogged)
		{
			if (RefreshToken())
			{
                                // Thermostat is accessable thro Homestatus / Homesdata in New API
                                //Thermostat data is updated every 10 minutes
                                // https://api.netatmo.com/api/getthermostatsdata
//                                if ((sec_counter % 900 == 0) || (bFirstTimeTH))
//                                {
//                                        bFirstTimeTH = false;
//                                        if ((m_bPollThermostat) || (sec_counter % 1200 == 0))
//                                                //
//                                                GetThermostatDetails();
//                                                Log(LOG_STATUS, "Thermostat. %d",  m_isLogged);
//                                                //
//                                }
				//
				//if ((m_bPollHomeData) || (sec_counter % 1200 == 0))
                                //        GetHomeDetails();
				//Weather / HomeCoach data is updated every 10 minutes
				// 03/03/2022 - PP Changing the Weather polling from 600 to 900s. This has reduce the number of server errors, 
				// but do not prevennt to have one time to time
				if ((sec_counter % 900 == 0) || (bFirstTimeWS))
				{
					bFirstTimeWS = false;
					if ((m_bPollWeatherData) || (sec_counter % 1200 == 0))
						// ParseStationData
						GetWeatherDetails();
					        Log(LOG_STATUS,"LOGGED W %d",  m_isLogged);
                                                Debug(DEBUG_HARDWARE, "Home Weather %s", m_Home_ID.c_str());
				}

				if ((sec_counter % 900 == 0) || (bFirstTimeHS))
				{
					bFirstTimeHS = false;
					if ((m_bPollHomecoachData) || (sec_counter % 1200 == 0))
						// ParseStationData
						GetHomecoachDetails();
                                                Log(LOG_STATUS,"logged HC %d",  m_isLogged);
                                                Debug(DEBUG_HARDWARE, "Home HC %s", m_Home_ID.c_str());
				}

				if ((sec_counter % 900 == 0) || (bFirstTimeSS))
				{
					bFirstTimeSS = false;
					if ((m_bPollHomeStatus) || (sec_counter % 1200 == 0))
						// GetHomesDataDetails
						GetHomeStatusDetails();
                                                Log(LOG_STATUS,"logged HS %d",  m_isLogged);
                                                Debug(DEBUG_HARDWARE, "Home HS %s", m_Home_ID.c_str());
				}
				
				//
				//Update Thermostat data when the
				//manual set point reach its end
				if (m_bForceSetpointUpdate)
				{
					time_t atime = time(nullptr);
					if (atime >= m_tSetpointUpdateTime)
					{
						m_bForceSetpointUpdate = false;
						if (m_bPollThermostat)
							//Needs function  -  TO DO
                                                        Debug(DEBUG_HARDWARE, "Home HS %s", m_Home_ID.c_str());
//							GetThermostatDetails();
					}
				}
			}
		}
	}
	Log(LOG_STATUS, "Worker stopped...");
}

/// <summary>
/// Login to Netatmon API
/// </summary>
/// <returns>true if logged in, false otherwise</returns>
bool CNetatmo::Login()
{
	//Already logged noting
	if (m_isLogged)
		return true;

	//Check if a stored token is available
	if (LoadRefreshToken())
	{
		//Yes : we refresh our take
		if (RefreshToken(true))
		{
			m_isLogged = true;
			m_bPollThermostat = true;
			return true;
		}
	}

	//Loggin on the API
	std::stringstream sstr;
	sstr << "grant_type=authorization_code&";
	sstr << "client_id=" << m_clientId << "&";
	sstr << "client_secret=" << m_clientSecret << "&";
	sstr << "code=" << m_authCode << "&";
	sstr << "redirect_uri=" << m_redirectUri << "&";
	sstr << "scope=" << m_scopes;

	std::string httpData = sstr.str();
	std::vector<std::string> ExtraHeaders;

//	ExtraHeaders.push_back("Host: api.netatmo.net");
//	ExtraHeaders.push_back("Content-Type: application/x-www-form-urlencoded;charset=UTF-8");

	std::string httpUrl(NETATMO_OAUTH2_TOKEN_URI);
	std::string sResult;
	bool ret = HTTPClient::POST(httpUrl, httpData, ExtraHeaders, sResult);

	//Check for returned data
	if (!ret)
	{
		Log(LOG_ERROR, "Error connecting to Server...");
		return false;
	}

	//Check the returned JSON
	Json::Value root;
	ret = ParseJSon(sResult, root);
	if ((!ret) || (!root.isObject()))
	{
		Log(LOG_ERROR, "Invalid/no data received...");
		return false;
	}
	//Check if access was granted
	if (root["access_token"].empty() || root["expires_in"].empty() || root["refresh_token"].empty())
	{
		Log(LOG_ERROR, "No access granted, check credentials...");
		Debug(DEBUG_HARDWARE, "No access granted, check credentials...(%s)(%s)", httpData.c_str(), root.toStyledString().c_str());
		return false;
	}

	//Initial Access Token
	m_accessToken = root["access_token"].asString();
	m_refreshToken = root["refresh_token"].asString();

	int expires = root["expires_in"].asInt();
	m_nextRefreshTs = mytime(nullptr) + expires;

	//Store the token in database in case
	//of domoticz restart
	StoreRefreshToken();
	m_isLogged = true;
	return true;
}

/// <summary>
/// Refresh a token previously granted by loggin to the API
/// (it avoid the need to submit username / password again)
/// </summary>
/// <param name="bForce">set to true to force refresh</param>
/// <returns>true if token refreshed, false otherwise</returns>
bool CNetatmo::RefreshToken(const bool bForce)
{
	//To refresh a token, we must have
	//one to refresh...
	if (m_refreshToken.empty())
		return false;

	//Check if we need to refresh the
	//token (token is valid for a fixed duration)
	if (!bForce)
	{
		if (!m_isLogged)
			return false;
		if ((mytime(nullptr) - 15) < m_nextRefreshTs)
			return true; //no need to refresh the token yet
	}

	// Time to refresh the token
	std::stringstream sstr;
	sstr << "grant_type=refresh_token&";
	sstr << "refresh_token=" << m_refreshToken << "&";
	sstr << "client_id=" << m_clientId << "&";
	sstr << "client_secret=" << m_clientSecret;

	std::string httpData = sstr.str();
	std::vector<std::string> ExtraHeaders;

//	ExtraHeaders.push_back("Host: api.netatmo.net");
//	ExtraHeaders.push_back("Content-Type: application/x-www-form-urlencoded;charset=UTF-8");

	std::string httpUrl(NETATMO_OAUTH2_TOKEN_URI);
	std::string sResult;
        Debug(DEBUG_HARDWARE, "Refresh %s", httpUrl.c_str());
	
	bool ret = HTTPClient::POST(httpUrl, httpData, ExtraHeaders, sResult);

	//Check for returned data
	if (!ret)
	{
		Log(LOG_ERROR, "Error connecting to Server...");
		return false;
	}

	//Check for valid JSON
	Json::Value root;
	ret = ParseJSon(sResult, root);
	if ((!ret) || (!root.isObject()))
	{
		Log(LOG_ERROR, "Invalid/no data received...");
		//Force login next time
		m_isLogged = false;
		return false;
	}

	//Check if token was refreshed and access granted
	if (root["access_token"].empty() || root["expires_in"].empty() || root["refresh_token"].empty())
	{
		//Force login next time
		Log(LOG_ERROR, "No access granted, forcing login again...");
		m_isLogged = false;
		return false;
	}

	//store the token
	m_accessToken = root["access_token"].asString();
	m_refreshToken = root["refresh_token"].asString();
	int expires = root["expires_in"].asInt();
	//Store the duration of validity of the token
	m_nextRefreshTs = mytime(nullptr) + expires;

	return true;
}

/// <summary>
/// Load an access token from the database
/// </summary>
/// <returns>true if token retreived, store the token in member variables</returns>
bool CNetatmo::LoadRefreshToken()
{
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT Extra FROM Hardware WHERE (ID==%d)", m_HwdID);
	if (result.empty())
		return false;
	std::string refreshToken = result[0][0];
	if (refreshToken.empty())
		return false;
	m_refreshToken = refreshToken;
	return true;
}

/// <summary>
/// Store an access token in the database for reuse after domoticz restart
/// (Note : we should also store token duration)
/// </summary>
void CNetatmo::StoreRefreshToken()
{
	if (m_refreshToken.empty())
		return;
	m_sql.safe_query("UPDATE Hardware SET Extra='%q' WHERE (ID == %d)", m_refreshToken.c_str(), m_HwdID);
}

/// <summary>
/// Upon domoticz devices action (pressing a switch) take action
/// on the netatmo thermostat valve through the API
/// </summary>
/// <param name="pdata">RAW data from domoticz device</param>
/// <param name=""></param>
/// <returns>success carrying the action (visible through domoticz)</returns>
bool CNetatmo::WriteToHardware(const char* pdata, const unsigned char /*length*/)
{
	//Check if a thermostat / valve device is available at all
	if ((m_thermostatDeviceID.empty()) || (m_thermostatModuleID.empty()))
	{
		Log(LOG_ERROR, "NetatmoThermostat: No thermostat found in online devices!");
		return false;
	}

	//Get a common structure to identify the actual action
	//the user has selected in domoticz (actionning a switch....)
	//Here a LIGHTING2 is used as we have selector switch for
	//our thermostat / valve devices
	const tRBUF* pCmd = reinterpret_cast<const tRBUF*>(pdata);
	if (pCmd->LIGHTING2.packettype != pTypeLighting2 &&
		(pCmd->LIGHTING2.packettype != pTypeGeneralSwitch && pCmd->LIGHTING2.subtype != sSwitchTypeSelector))
		return false;

	//This is the boiler status switch : do nothing
	// unitcode == 0x00 ### means boiler_status switch
	if ((int)(pCmd->LIGHTING2.unitcode) == 0)
		return true;

	//This is the selector switch for setting the thermostat schedule
	// unitcode == 0x02 ### means schedule switch
	if ((int)(pCmd->LIGHTING2.unitcode) == 2)
	{
		//Recast raw data to get switch specific data
		const _tGeneralSwitch* xcmd = reinterpret_cast<const _tGeneralSwitch*>(pdata);
		int ID = xcmd->id; //switch ID
		int level = xcmd->level; //Level selected on the switch

		//Set the schedule on the thermostat
		SetSchedule(level);

		return true;
	}

	//Set Away mode on thermostat
	// unitcode == 0x03 ### means mode switch
	if ((int)(pCmd->LIGHTING2.unitcode) == 3)
	{
		//Switch active = Turn on Away Mode
		bool bIsOn = (pCmd->LIGHTING2.cmnd == light2_sOn);
		//Recast raw data to get switch specific data
		const _tGeneralSwitch* xcmd = reinterpret_cast<const _tGeneralSwitch*>(pdata);
		int ID = xcmd->id;// switch ID
		int level = (xcmd->level);//Level selected on the switch

		// Get the mode ID and thermostat ID
		int mode = (level / 10) - 1;
		unsigned long therm_idx = (pCmd->LIGHTING2.id1 << 24) + (pCmd->LIGHTING2.id2 << 16) + (pCmd->LIGHTING2.id3 << 8) + pCmd->LIGHTING2.id4;

		//Set mode on the thermostat
		return SetProgramState(therm_idx, mode);
	}

	return false;
}

/// <summary>
/// Set the thermostat / valve in "away mode"
/// </summary>
/// <param name="idx">ID of the device to set in away mode</param>
/// <param name="bIsAway">wether to put in away or normal / schedule mode</param>
/// <returns>success status</returns>
bool CNetatmo::SetAway(const int idx, const bool bIsAway)
{
	return SetProgramState(idx, (bIsAway == true) ? 1 : 0);
}

/// <summary>
/// Set the thermostat / valve operationnal mode
/// </summary>
/// <param name="idx">ID of the device to put in away mode</param>
/// <param name="newState">Mode of the device (0 = schedule / normal; 1 = away mode; 2 = frost guard; 3 = off (not supported in new API)</param>
/// <returns>success status</returns>
bool CNetatmo::SetProgramState(const int idx, const int newState)
{
	//Check if logged, logging if needed
	if (!m_isLogged == true)
	{
		if (!Login())
			return false;
	}

	std::vector<std::string> ExtraHeaders;
	std::string sResult;

	if (m_energyType != NETYPE_THERMOSTAT)
	{
		// Check if thermostat device is available, reversing byte order to get our ID
		int reverseIdx = ((idx >> 24) & 0x000000FF) | ((idx >> 8) & 0x0000FF00) | ((idx << 8) & 0x00FF0000) | ((idx << 24) & 0xFF000000);
		if ((m_thermostatDeviceID[reverseIdx].empty()) || (m_thermostatModuleID[reverseIdx].empty()))
		{
			Log(LOG_ERROR, "NetatmoThermostat: No thermostat found in online devices!");
			return false;
		}

		std::string thermState;
		switch (newState)
		{
		case 0:
			thermState = "program"; //The Thermostat is currently following its weekly schedule
			break;
		case 1:
			thermState = "away"; //The Thermostat is currently applying the away temperature
			break;
		case 2:
			thermState = "hg"; //he Thermostat is currently applying the frost-guard temperature
			break;
		case 3:
			thermState = "off"; //The Thermostat is off
			break;
		default:
			Log(LOG_ERROR, "NetatmoThermostat: Invalid thermostat state!");
			return false;
		}
//		ExtraHeaders.push_back("Host: api.netatmo.net");
//		ExtraHeaders.push_back("Content-Type: application/x-www-form-urlencoded;charset=UTF-8");

		std::stringstream sstr;
		sstr << "access_token=" << m_accessToken;
		sstr << "&device_id=" << m_thermostatDeviceID[reverseIdx];
		sstr << "&module_id=" << m_thermostatModuleID[reverseIdx];
		sstr << "&setpoint_mode=" << thermState;

		std::string httpData = sstr.str();

		std::stringstream bstr;
		bstr << NETATMO_API_URI;
                bstr << "api/setthermpoint";

		std::string httpUrl = bstr.str();

		if (!HTTPClient::POST(httpUrl, httpData, ExtraHeaders, sResult))
		{
			Log(LOG_ERROR, "NetatmoThermostat: Error setting setpoint state!");
			return false;
		}
	}
	else
	{
		std::string thermState;
		switch (newState)
		{
		case 0:
			thermState = "schedule"; //The Thermostat is currently following its weekly schedule
			break;
		case 1:
			thermState = "away"; //The Thermostat is currently applying the away temperature
			break;
		case 2:
			thermState = "hg";
			break;
		default:
			Log(LOG_ERROR, "NetatmoThermostat: Invalid thermostat state!");
			return false;
		}
		std::string sPostData = "access_token=" + m_accessToken + m_Home_ID + "&mode=" + thermState;

		std::stringstream bstr;
		bstr << NETATMO_API_URI;
                bstr << "api/setthermpoint";
//                bstr << "setroomthermpoint";

                std::string httpUrl = bstr.str();
		
		if (!HTTPClient::POST(httpUrl, sPostData, ExtraHeaders, sResult))
		{
			Log(LOG_ERROR, "NetatmoThermostat: Error setting setpoint state!");
			return false;
		}
	}

	//GetThermostatDetails();
	return true;
}

/// <summary>
/// Set temperture override on thermostat / valve for
/// one hour
/// </summary>
/// <param name="idx">ID of the device to override temp</param>
/// <param name="temp">Temperature to set</param>
void CNetatmo::SetSetpoint(int idx, const float temp)
{
	//Check if still connected to the API
	//connect to it if needed
	if (!m_isLogged == true)
	{
		if (!Login())
			return;
	}

	//Temp to set
	float tempDest = temp;
	unsigned char tSign = m_sql.m_tempsign[0];
	// convert back to Celsius
	if (tSign == 'F')
		tempDest = static_cast<float>(ConvertToCelsius(tempDest));

	//We change the setpoint for one hour
	time_t now = mytime(nullptr);
	struct tm etime;
	localtime_r(&now, &etime);
	time_t end_time;
	int isdst = etime.tm_isdst;
	bool goodtime = false;
	while (!goodtime) {
		etime.tm_isdst = isdst;
		etime.tm_hour += 1;
		end_time = mktime(&etime);
		goodtime = (etime.tm_isdst == isdst);
		isdst = etime.tm_isdst;
		if (!goodtime)
			localtime_r(&now, &etime);
	}

	std::vector<std::string> ExtraHeaders;
	std::string sResult;
	std::stringstream sstr;
	std::stringstream bstr;

	bool ret = false;
	if (m_energyType != NETYPE_THERMOSTAT)
	{
		// Check if thermostat device is available
		if ((m_thermostatDeviceID[idx].empty()) || (m_thermostatModuleID[idx].empty()))
		{
			Log(LOG_ERROR, "NetatmoThermostat: No thermostat found in online devices!");
			return;
		}

//		ExtraHeaders.push_back("Host: api.netatmo.net");
//		ExtraHeaders.push_back("Content-Type: application/x-www-form-urlencoded;charset=UTF-8");

		sstr << "access_token=" << m_accessToken;
		sstr << "&device_id=" << m_thermostatDeviceID[idx];
		sstr << "&module_id=" << m_thermostatModuleID[idx];
		sstr << "&setpoint_mode=manual&setpoint_temp=" << tempDest;
		sstr << "&setpoint_endtime=" << end_time;

		std::string httpData = sstr.str();

		bstr << NETATMO_API_URI;
                bstr << "api/setthermpoint";

                std::string httpUrl = bstr.str();
		Debug(DEBUG_HARDWARE, "SetSetpoint %s", httpUrl.c_str());
		
		ret = HTTPClient::POST(httpUrl, httpData, ExtraHeaders, sResult);
	}
	else
	{
		//find roomid
		std::string roomID = m_thermostatDeviceID[idx];
		if (roomID.empty())
		{
			Log(LOG_ERROR, "NetatmoThermostat: No thermostat or valve found in online devices!");
			return;
		}

		sstr << "access_token=" << m_accessToken;
		sstr << m_Home_ID;
		sstr << "&room_id=" << roomID;
		sstr << "&mode=manual&temp=" << tempDest;
		sstr << "&endtime=" << end_time;

		std::string sPostData = sstr.str();

		bstr << NETATMO_API_URI;
                bstr << "api/setthermmode";

                std::string httpUrl = bstr.str();
		Debug(DEBUG_HARDWARE, "SetSetpoint %s", httpUrl.c_str());
		
		ret = HTTPClient::POST(httpUrl, sPostData, ExtraHeaders, sResult);
	}

	if (!ret)
	{
		Log(LOG_ERROR, "NetatmoThermostat: Error setting setpoint!");
		return;
	}

	//Retrieve new thermostat data
	//GetThermostatDetails();
	//Set up for updating thermostat data when the set point reach its end
	m_tSetpointUpdateTime = time(nullptr) + 60;
	m_bForceSetpointUpdate = true;
}

/// <summary>
/// Change the schedule of the thermostat (new API only)
/// </summary>
/// <param name="scheduleId">ID of the schedule to use</param>
/// <returns>success status</returns>
bool CNetatmo::SetSchedule(int scheduleId)
{
	//Checking if we are still connected to the API
	if (!m_isLogged == true)
	{
		if (!Login())
			return false;
	}

	//Setting the schedule only if we have
	//the right thermostat type
	std::stringstream bstr;
	if (m_energyType == NETYPE_THERMOSTAT)
	{
		std::string sResult;
		std::string thermState = "schedule";
		std::vector<std::string> ExtraHeaders;
//		ExtraHeaders.push_back("Host: api.netatmo.net");
//		ExtraHeaders.push_back("Content-Type: application/x-www-form-urlencoded;charset=UTF-8");

		std::string sPostData = "access_token=" + m_accessToken + m_Home_ID + "&mode=" + thermState + "&schedule_id=" + m_ScheduleIDs[scheduleId];

		bstr << NETATMO_API_URI;
                bstr << "api/setthermmode";

                std::string httpUrl = bstr.str();
		
		if (!HTTPClient::POST(httpUrl, sPostData, ExtraHeaders, sResult))
		{
			Log(LOG_ERROR, "NetatmoThermostat: Error setting setpoint state!");
			return false;
		}
		//store the selected schedule in our local data to avoid
		//changing back the schedule when using away mode
		m_selectedScheduleID = scheduleId;
	}

	return true;
}

/// <summary>
/// Utility to make the URI based on the type of device we
/// want to retrieve initial data
/// </summary>
/// <param name="NType">Netatmo device type</param>
/// <returns>API response</returns>
std::string CNetatmo::MakeRequestURL(const _eNetatmoType NType, std::string data)
{
	std::stringstream sstr;

	switch (NType)
	{
	case NETYPE_MEASURE:
		sstr << NETATMO_API_URI;
                sstr << "api/getmeasure";
                //"https://api.netatmo.com/api/getmeasure";
		break;
	case NETYPE_WEATHER_STATION:
		sstr << NETATMO_API_URI;
                sstr << "api/getstationsdata";
                //"https://api.netatmo.com/api/getstationsdata";
		break;
	case NETYPE_AIRCARE:
		sstr << NETATMO_API_URI;
                sstr << "api/gethomecoachsdata";
                //"https://api.netatmo.com/api/gethomecoachsdata";
		break;
	case NETYPE_THERMOSTAT:         // OLD API
		sstr << NETATMO_API_URI;
                sstr << "api/getthermostatsdata";
                //"https://api.netatmo.com/api/getthermostatsdata";
		break;
	case NETYPE_HOME:               // OLD API
		sstr << NETATMO_API_URI;
                sstr << "api/homedata";
                //"https://api.netatmo.com/api/homedata";
		break;
	case NETYPE_HOMESDATA:          // was NETYPE_ENERGY
		sstr << NETATMO_API_URI;
                sstr << "api/homesdata";
                //"https://api.netatmo.com/api/homesdata";
		break;
	case NETYPE_STATUS:
		sstr << NETATMO_API_URI;
                sstr << "api/homestatus";
                //"https://api.netatmo.com/api/homestatus";
		break;
	case NETYPE_CAMERAS:            // OLD API
		sstr << NETATMO_API_URI;
                sstr << "api/getcamerapicture";
                //"https://api.netatmo.com/api/getcamerapicture";
		break;
	case NETYPE_EVENTS:
		sstr << NETATMO_API_URI;
                sstr << "api/geteventsuntil";
                //"https://api.netatmo.com/api/geteventsuntil";
		break;
        case NETYPE_SETSTATE:
                sstr << NETATMO_API_URI;
                sstr << "api/setstate";
                //"https://api.netatmo.com/api/setstate"
                break;
        case NETYPE_SETROOMTHERMPOINT:
                sstr << NETATMO_API_URI;
                sstr << "api/setroomthermpoint";
                //"https://api.netatmo.com/api/setroomthermpoint"
                break;
        case NETYPE_SETTHERMMODE:
                sstr << NETATMO_API_URI;
                sstr << "api/setthermmode";
                //"https://api.netatmo.com/api/setthermmode";
                break;
        case NETYPE_SETPERSONSAWAY:
                sstr << NETATMO_API_URI;
                sstr << "api/setpersonsaway";
                //"https://api.netatmo.com/api/setpersonsaway";
                break;
        case NETYPE_SETPERSONSHOME:
                sstr << NETATMO_API_URI;
                sstr << "api/setpersonshome";
                //"https://api.netatmo.com/api/setpersonshome"
                break;
        case NETYPE_NEWHOMESCHEDULE:
                sstr << NETATMO_API_URI;
                sstr << "api/createnewhomeschedule";
                //"https://api.netatmo.com/api/createnewhomeschedule"
                break;
        case NETYPE_SYNCHOMESCHEDULE:
                sstr << NETATMO_API_URI;
                sstr << "api/synchomeschedule";
                //"https://api.netatmo.com/api/synchomeschedule"
                break;
        case NETYPE_SWITCHHOMESCHEDULE:
                sstr << NETATMO_API_URI;
                sstr << "api/switchhomeschedule";
                //"https://api.netatmo.com/api/switchhomeschedule"
                break;
        case NETYPE_ADDWEBHOOK:
                sstr << NETATMO_API_URI;
                sstr << "api/addwebhook";
                //"https://api.netatmo.com/api/addwebhook"
                break;
        case NETYPE_DROPWEBHOOK:
                sstr << NETATMO_API_URI;
                sstr << "api/dropwebhook";
                //"https://api.netatmo.com/api/dropwebhook"
                break;
        case NETYPE_PUBLICDATA:
                sstr << NETATMO_API_URI;
                sstr << "api/getpublicdata";
                //"https://api.netatmo.com/api/getpublicdata";
                break;
		
	default:
		return "";
	}

	sstr << "?";
	sstr << data;
        sstr << "&";
        sstr << "access_token=" << m_accessToken;
	sstr << "&";
	sstr << "get_favorites=" << "true";
	return sstr.str();
}


/// <summary>
/// Get API
/// </summary>
void CNetatmo::Get_Respons_API(const _eNetatmoType& NType, std::string& sResult, std::string& home_id , bool& bRet, Json::Value& root )
{
        //
        //Check if connected to the API
        if (!m_isLogged)
                return;
        //Locals
        std::string httpUrl;                             //URI to be tested
        std::string data;

        std::stringstream sstr;
        sstr << "";
        std::vector<std::string> ExtraHeaders;           // HTTP Headers
        //
        httpUrl = MakeRequestURL(NType, home_id);

        std::string sPostData = "";
        Debug(DEBUG_HARDWARE, "Respons URL   %s", httpUrl.c_str());
        //
        if (!HTTPClient::POST(httpUrl, sPostData,  ExtraHeaders, sResult))
        {
                Log(LOG_ERROR, "Error connecting to Server...");
                return ;
        }
        //Check for error
        bRet = ParseJSon(sResult, root);
        if ((!bRet) || (!root.isObject()))
        {
                Log(LOG_ERROR, "Invalid data received...");
                return ;
        }
        if (!root["error"].empty())
        {
                //We received an error
                Log(LOG_ERROR, "Error %s", root["error"]["message"].asString().c_str());
                m_isLogged = false;
                return ;
        }
        //
}


/// <summary>
/// Get details for home         // OLD API
/// </summary>
bool CNetatmo::GetHomeDetails()
{
	//Check if connected to the API
	if (!m_isLogged)
	{
		return false;
	}
	//Locals
	std::string sResult; // text returned by API
	Json::Value root;    // root JSON object
	std::string home_id = "";
	bool bRet;           //Parsing status
	//
        Get_Respons_API(NETYPE_HOME, sResult, home_id, bRet, root);
	
        if (!root["body"]["homes"].empty())
	{
                //
                bRet = ParseHomeData(sResult);
                if (bRet)
                {
                        // Data was parsed with success
                        Log(LOG_STATUS, "Home Data parsed");
                }
         }
}


/// <summary>
/// Get details for home
/// </summary>
void CNetatmo::GetHomesDataDetails()
{
	//Locals
	std::string sResult;      // text returned by API
	std::string home_id = ""; //m_Home_ID; //Home ID
        bool bRet;                //Parsing status
	Json::Value root;         // root JSON object

        Get_Respons_API(NETYPE_HOMESDATA, sResult, home_id, bRet, root);
	//
        if (!root["body"]["homes"].empty())
	{
                //
                for (auto home : root["body"]["homes"])
                {
                        if (!home["id"].empty())
                        {
                                // Home ID from Homesdata
                                m_Home_ID = "home_id=" + home["id"].asString();
                                std::string Home_Name = home["name"].asString();
                                Log(LOG_STATUS, "Home id %s updated.", Home_Name.c_str());

                                //Get the rooms
                                if (!home["rooms"].empty())
                                {
                                        for (auto room : home["rooms"])
                                        {
                                                 //Json::Value room = home["rooms"];
                                                 std::string roomID = room["id"].asString();
                                                 m_RoomNames[roomID] = room["name"].asString();
                                                 std::string roomTYPE = room["type"].asString();
                                                 m_Room_Type[roomID] = roomTYPE;

                                                 int crcId;
                                                 for (auto module_ids : room["module_ids"])
                                                 {
                                                         //
                                                       m_Room[roomID] = module_ids.toStyledString();
                                                         //Debug(DEBUG_HARDWARE, "RoomID %s", roomID.c_str());
                                                         crcId = Crc32(0, (const unsigned char*)roomID.c_str(), roomID.length());
                                                         m_ModuleIDs[roomID] = crcId;    //For temperature devices in Rooms
                                                         m_RoomIDs[roomID] = crcId;
                                                 }
                                                 if (!room["category"].empty())
                                                        m_Module_category[roomID] = room["category"].asString();
                                                 Debug(DEBUG_HARDWARE, "mID %s - Type %s - Name %s - categorie %s - crcID %d", roomID.c_str(), roomTYPE.c_str(), m_RoomNames[roomID].c_str(), m_Module_category[roomID].c_str(), crcId );
                                        }
                                }
                                //Get the module names
                                if (!home["modules"].empty())
                                {
                                        for (auto module : home["modules"])
                                        {
                                                //Debug(DEBUG_HARDWARE, "Home Modules ");
                                                if (!module["id"].empty())
                                                {
                                                        std::string type = module["type"].asString();
                                                        std::string mID = module["id"].asString();
                                                        //Debug(DEBUG_HARDWARE, "Type %s", type.c_str());
                                                        m_ModuleNames[mID] = module["name"].asString();
                                                        int crcId = Crc32(0, (const unsigned char*)mID.c_str(), mID.length());
                                                        m_ModuleIDs[mID] = crcId;
                                                        Debug(DEBUG_HARDWARE, "mID %s - Type %s - Name %s - crcID %d", mID.c_str(), type.c_str(), m_ModuleNames[mID].c_str(), crcId );
                                                        //Debug(DEBUG_HARDWARE, "Name %s", module["name"].asString().c_str());
                                                        //Store thermostate name for later naming switch / sensor
                                                        if (module["type"] == "NATherm1")
                                                                m_ThermostatName[mID] = module["name"].asString();
                                                        if (module["type"] == "NRV")
                                                                m_ThermostatName[mID] = module["name"].asString();
                                                }
                                        }
                                }
                                //Get the Persons
                                if (!home["persons"].empty())
                                {
                                        for (auto person : home["persons"])
                                        {
                                                if (!person["id"].empty())
                                                {
                                                        std::string mID = person["id"].asString();
                                                        m_PersonsNames[mID] = person["pseudo"].asString();
                                                        m_PersonUrl[mID] = person["url"].toStyledString();
                                                        int crcId = Crc32(0, (const unsigned char*)mID.c_str(), mID.length());
                                                        m_PersonsIDs[mID] = crcId;
                                                        Debug(DEBUG_HARDWARE, "mID %s - Name %s - crcID %d", mID.c_str(), m_PersonsNames[mID].c_str(), crcId );
                                                        //
                                                 }
                                        }
                                }
                                //Get the schedules
                                if (!home["schedules"].empty())
                                {
                                        for (auto schedule : home["schedules"])
                                        {
                                                 //mRoot = ["schedules"];
                                                 std::string allSchName = "Off";
                                                 std::string allSchAction = "00";
                                                 int index = 0;
                                                 std::string sID = schedule["id"].asString();
                                                 index += 10;
                                                 int crcId = Crc32(0, (const unsigned char*)sID.c_str(), sID.length());
                                                 m_ScheduleNames[index] = schedule["name"].asString();
                                                 m_ScheduleIDs[index] = sID;
                                                 if (!schedule["selected"].empty() && schedule["selected"].asBool())
                                                        m_selectedScheduleID = index;
                                                 Debug(DEBUG_HARDWARE, "sID %s - Name %s - crcID %d", sID.c_str(), m_ScheduleNames[index].c_str(), crcId );
                                        }
                                }
                        }

                // Data was parsed with success
                Log(LOG_STATUS, "Homes Data parsed");
                }
	}
}

/// <summary>
/// Get details for weather station
/// </summary>
void CNetatmo::GetWeatherDetails()
{
	//Check if connected to the API
	if (!m_isLogged)
		return;
	//
	if (m_bFirstTimeWeatherData)
	{
                std::string sResult; // text returned by API
                std::string home_id = "";
                bool bRet;           //Parsing
                Json::Value root;    // root JSON object
		
                Get_Respons_API(NETYPE_WEATHER_STATION, sResult, home_id, bRet, root);
		//
		//Parse API response
		bRet = ParseStationData(sResult, false);
//		if (bRet)
//		{
//			// Data was parsed with success so we have our device
//			m_weatherType = NETYPE_WEATHER_STATION;
//		}
		m_bPollWeatherData = false;
	}
//	m_bFirstTimeWeatherData = false;
}



/// <summary>
/// Get details for homecoach
/// </summary>
void CNetatmo::GetHomecoachDetails()
{
	//Check if connected to the API
	if (!m_isLogged)
		return;
	//Locals
	std::string sResult; // text returned by API
	std::string home_id = "";
	bool bRet;           //Parsing status
	Json::Value root;    // root JSON object
	//
        Get_Respons_API(NETYPE_HOMECOACH, sResult, home_id, bRet, root);

	//Parse API response
	bRet = ParseStationData(sResult, false);
//	if (bRet)
//		{
//			// Data was parsed with success so we have our device
//			m_weatherType = NETYPE_HOMECOACH;
//		}
	m_bPollHomecoachData = false;
}



/// <summary>
/// Get details for thermostat / valve
/// </summary>



/// <summary>
/// Get details for homeStatus
/// </summary>
void CNetatmo::GetHomeStatusDetails()
{
	//Check if connected to the API
	if (!m_isLogged)
		return;

	//Locals
	std::string sResult;                   // text returned by API
	std::string home_id = m_Home_ID + "&"; //Home ID
	bool bRet;                             //Parsing status
	Json::Value root;                      // root JSON object
	//
        GetHomesDataDetails();
        //
        Get_Respons_API(NETYPE_STATUS, sResult, home_id, bRet, root);

	//Parse API response
	bRet = ParseStationData(sResult, false);
//	if (bRet)
//		{
//			// Data was parsed with success so we have our device
//			m_weatherType = NETYPE_STATUS;
//		}
	m_bPollHomeStatus = false;
}


/// <summary>
/// Get Historical data from Netatmo Device
/// </summary>
void CNetatmo::Get_Measure()
{
        //Check if connected to the API
        if (!m_isLogged)
                return ;
        //Locals
        std::string sResult; // text returned by API
        Json::Value root;    // root JSON object
        std::string home_id = "";
        bool bRet;           //Parsing status
        std::string gateway = " ";
        std::string module_id = " ";
        std::string scale = "30min";  //{30min, 1hour, 3hours, 1day, 1week, 1month}
        std::string measure = NETYPE_MEASURE + "device_id=" + gateway + "&module_id=" + module_id + "&scale=" + scale + "&type=sum_boiler_off&type=sum_boiler_on&type=boileroff&type=boileron&optimize=false&real_time=false";
        //
        //Respons::Get_Respons_API(const CNetatmo& NETYPE_MEASURE, sResult, home_id, bRet, root, m_accessToken);
        Get_Respons_API(NETYPE_MEASURE, sResult, home_id, bRet, root);

        if (!root["body"].empty())
        {
                //https://api.netatmo.com/api/getmeasure?device_id=< >&module_id=< >&scale=30min&type=sum_boiler_off&type=sum_boiler_on&type=boileroff&type=boileron&optimize=false&real_time=false
                //bRet = ParseData(sResult);     //
                //if (bRet)
                //{
                        // Data was parsed with success
                Log(LOG_STATUS, "Measure Data parsed");
                //}
         }
}


/// <summary>
/// Get camera picture           // OLD API
/// </summary>
void CNetatmo::Get_Picture()
{
        //Check if connected to the API
        if (!m_isLogged)
                return ;

        //Locals
        std::string sResult; // text returned by API
        Json::Value root;    // root JSON object
        std::string home_id = "";
        bool bRet;           //Parsing status
        //
        //Respons::Get_Respons_API(const CNetatmo& NETYPE_CAMERAS, sResult, home_id, bRet, root, m_accessToken);
        Get_Respons_API(NETYPE_CAMERAS, sResult, home_id, bRet, root);

        if (!root["body"]["homes"].empty())
        {
                //ParseHomeData(sResult)
                bRet = ParseHomeData(sResult);
                if (bRet)
                {
                        // Data was parsed with success
                        Log(LOG_STATUS, "Picture Data parsed");
                }
         }
}


/// <summary>
/// Get events           // OLD API
/// </summary>
void CNetatmo::Get_Events()
{
        //Check if connected to the API
        if (!m_isLogged)
                return ;

        //Locals
        std::string sResult; // text returned by API
        Json::Value root;    // root JSON object
        std::string home_id = "";
        bool bRet;           //Parsing status

        Get_Respons_API(NETYPE_EVENTS, sResult, home_id, bRet, root);

        if (!root["body"]["homes"].empty())
        {
                //ParseHomeData(sResult)
                //bRet = ParseHomeData(sResult);
                //if (bRet)
                //{
                        // Data was parsed with success
                Log(LOG_STATUS, "Events Data parsed");
                //}
         }
}


/// <summary>
/// Parse data for weather station and thermostat (with old API)
/// </summary>
/// <param name="sResult">JSON raw data to be parsed</param>
/// <param name="bIsThermostat">set to true if a thermostat is available</param>
/// <returns>success retreiving and parsing data status</returns>
bool CNetatmo::ParseStationData(const std::string& sResult, const bool bIsThermostat)
{
	//Check for well formed JSON data
	//and devices objects in the JSON reply
	Json::Value root;
	bool ret = ParseJSon(sResult, root);
	if ((!ret) || (!root.isObject()))
	{
		Log(LOG_STATUS, "Invalid data received...");
		return false;
	}
	bool bHaveDevices = true;
	//
	if (root["body"].empty())
	{
                Debug(DEBUG_HARDWARE, "Body empty");
		bHaveDevices = false;
	}
	else if (root["body"]["devices"].empty())
	{
                Debug(DEBUG_HARDWARE, "Devices empty");
		bHaveDevices = false;
	}
	else if (!root["body"]["devices"].isArray())
	{
                Debug(DEBUG_HARDWARE, "Devices no Array");
		bHaveDevices = false;
	}
        // Homecoach        body - devices
        // WeatherStation   body - devices
        // HomesData     is body - homes
        // HomeStatus    is body - home
	
	//Return error if no devices are found
	if (!bHaveDevices)
	{
		Log(LOG_STATUS, "No devices found...");
		if ((!bIsThermostat) && (!m_bFirstTimeWeatherData) && (m_bPollWeatherData) && (m_bPollHomecoachData) ) //&& (m_bPollHomeStatus) && (m_bPollco2Data))
		{
			// Do not warn if we check if we have a Thermostat device
			Log(LOG_STATUS, "No Weather Station devices found...");
		}
		return false;
	}

	//Get data for the devices
	std::vector<_tNetatmoDevice> _netatmo_devices;
	int iDevIndex = 0;
	for (auto device : root["body"]["devices"])
	{
		if (!device["_id"].empty())
		{
			//Main Weather Station
			std::string id = device["_id"].asString();
			std::string type = device["type"].asString();
			std::string name;
			int mrf_status = 0;
                        int RF_status = 0;
                        int mbattery_percent = 255;
			if (!device["module_name"].empty())
                                name = device["module_name"].asString();
                        else if (!device["station_name"].empty())
                                name = device["station_name"].asString();
                        else if (!device["name"].empty())
                                name = device["name"].asString();
                        else if (!device["modules"].empty())
				name = device["modules"]["module_name"].asString();
                        else
                                name = "UNKNOWN";
			
			//get Home ID from Weatherstation
                        if (type == "NAMain")
                                m_Home_ID = "home_id=" + device["home_id"].asString();
                                Debug(DEBUG_HARDWARE, "m_Home_ID = %s", m_Home_ID.c_str());

			// Station_name NOT USED ?
                        std::string station_name;

			//stdreplace(name, "'", "");
			//stdreplace(station_name, "'", "");
			_tNetatmoDevice nDevice;
			nDevice.ID = id;
			nDevice.ModuleName = name;
			nDevice.StationName = station_name;
                        int crcId = Crc32(0, (const unsigned char*)id.c_str(), id.length());
                        m_ModuleIDs[id] = crcId;

                        // Find the corresponding _tNetatmoDevice
                        bool bHaveFoundND = false;
                        iDevIndex = 0;
                        std::vector<_tNetatmoDevice>::const_iterator ittND;
                        for (ittND = _netatmo_devices.begin(); ittND != _netatmo_devices.end(); ++ittND)
                        {
                                std::vector<std::string>::const_iterator ittNM;
                                for (ittNM = ittND->ModulesIDs.begin(); ittNM != ittND->ModulesIDs.end(); ++ittNM)
                                {
                                        if (*ittNM == id)
                                        {
                                                nDevice = *ittND;
                                                iDevIndex = static_cast<int>(ittND - _netatmo_devices.begin());
                                                bHaveFoundND = true;
                                                break;
                                        }
                                }
                                if (bHaveFoundND == true)
                                        break;
                        }

                        if (!device["battery_percent"].empty())
                        {
                                mbattery_percent = device["battery_percent"].asInt(); //Batterij
                                Debug(DEBUG_HARDWARE, "batterij - bat %d", std::to_string(mbattery_percent));
                        }
                        if (!device["wifi_status"].empty())
                        {
                                // 86=bad, 56=good
                                RF_status = (86 - device["wifi_status"].asInt()) / 3;
                                if (RF_status > 10)
                                        RF_status = 10;
                        }
			if (!device["rf_status"].empty())
                        {
                                RF_status = (86 - device["rf_status"].asInt()) / 3;
                                if (RF_status > 10)
                                        RF_status = 10;
                        }
                        if (!device["dashboard_data"].empty())
                        {
                                //SaveString2Disk(device["dashboard_data"], std::string("../") + name.c_str() + ".txt");
                                ParseDashboard(device["dashboard_data"], iDevIndex, crcId, name, type, mbattery_percent, RF_status);
                        }
			//Weather modules (Temp sensor, Wind Sensor, Rain Sensor)
			if (!device["modules"].empty())
			{
				if (device["modules"].isArray())
				{
					// Add modules for this device
					for (auto module : device["modules"])
					{
						if (module.isObject())
						{
							if (module["_id"].empty())
							{
								iDevIndex++;
								continue;
							}
							std::string mid = module["_id"].asString();
							std::string mtype = module["type"].asString();
							std::string mname = module["module_name"].asString();
							int crcId = Crc32(0, (const unsigned char*)mid.c_str(), mid.length());
                                                        m_ModuleIDs[mid] = crcId;
							if (mname.empty())
								mname = "unknown" + mid;
							int mbattery_percent = 0;
							if (!module["battery_percent"].empty())
								mbattery_percent = module["battery_percent"].asInt();
							int mrf_status = 0;
							if (!module["rf_status"].empty())
							{
								// 90=low, 60=highest
								mrf_status = (90 - module["rf_status"].asInt()) / 3;
								if (mrf_status > 10)
									mrf_status = 10;
							}
							//
							if (!module["dashboard_data"].empty())
							{
								ParseDashboard(module["dashboard_data"], iDevIndex, crcId, mname, mtype, mbattery_percent, mrf_status);
							}
						}
						else
							nDevice.ModulesIDs.push_back(module.asString());
					}
				}
			}
			_netatmo_devices.push_back(nDevice);

		}
		iDevIndex++;
	}
	return true;
}


/// <summary>
/// Parse weather data for weather / homecoach station based on previously parsed JSON (with ParseStationData)
/// </summary>
/// <param name="root">JSON object to read</param>
/// <param name="DevIdx">Index of the device</param>
/// <param name="ID">ID of the module</param>
/// <param name="name">Name of the module (previously parsed)</param>
/// <param name="ModuleType">Type of module (previously parsed)</param>
/// <param name="battery_percent">battery percent (previously parsed)</param>
/// <param name="rssiLevel">radio network level (previously parsed)</param>
/// <returns>success retreiving and parsing data</returns>
bool CNetatmo::ParseDashboard(const Json::Value& root, const int DevIdx, const int ID, const std::string& name, const std::string& ModuleType, const int battery_percent, const int rssiLevel)
{
	//Local variable for holding data retreived
	bool bHaveTemp = false;
	bool bHaveHum = false;
	bool bHaveBaro = false;
	bool bHaveCO2 = false;
	bool bHaveRain = false;
	bool bHaveSound = false;
	bool bHaveWind = false;
	bool bHaveSetpoint = false;

	int temp = 0;
	float Temp = 0;
	int sp_temp = 0;
	int hum = 0;
	float baro = 0;
	int co2 = 0;
	float rain = 0;
	int sound = 0;

	int wind_angle = 0;
	float wind_strength = 0;
	float wind_gust = 0;
        int batValue = battery_percent;  // / 100;
//      batValue = GetBatteryLevel(ModuleType, battery_percent);

	// check for Netatmo cloud data timeout, except if we deal with a thermostat
	if (ModuleType != "NATherm1")
	{
		std::time_t tNetatmoLastUpdate = 0;
		std::time_t tNow = time(nullptr);

		// initialize the relevant device flag
		if (m_bNetatmoRefreshed.find(ID) == m_bNetatmoRefreshed.end())
			m_bNetatmoRefreshed[ID] = true;
		// Check when dashboard data was last updated
		if (!root["time_utc"].empty())
			tNetatmoLastUpdate = root["time_utc"].asUInt();
		Debug(DEBUG_HARDWARE, "Module [%s] last update = %s", name.c_str(), ctime(&tNetatmoLastUpdate));
		// check if Netatmo data was updated in the past 10 mins (+1 min for sync time lags)... if not means sensors failed to send to cloud
		if (tNetatmoLastUpdate > (tNow - 660))
		{
			if (!m_bNetatmoRefreshed[ID])
			{
				Log(LOG_STATUS, "cloud data for module [%s] is now updated again", name.c_str());
				m_bNetatmoRefreshed[ID] = true;
			}
		}
		else
		{
			if (m_bNetatmoRefreshed[ID])
				Log(LOG_ERROR, "cloud data for module [%s] no longer updated (module possibly disconnected)", name.c_str());
			m_bNetatmoRefreshed[ID] = false;
			return false;
		}
	}

	if (!root["Temperature"].empty())
	{
		bHaveTemp = true;
		temp = root["Temperature"].asInt();
	}
	else if (!root["temperature"].empty())
	{
		bHaveTemp = true;
		temp = root["temperature"].asFloat();
	}
	if (!root["Sp_Temperature"].empty())
	{
		bHaveSetpoint = true;
		sp_temp = root["Temperature"].asInt();
	}
	else if (!root["setpoint_temp"].empty())
	{
		bHaveSetpoint = true;
		sp_temp = root["setpoint_temp"].asInt();
	}
	if (!root["Humidity"].empty())
	{
		bHaveHum = true;
		hum = root["Humidity"].asInt();
	}
	if (!root["Pressure"].empty())
	{
		bHaveBaro = true;
		baro = root["Pressure"].asInt();
	}
	if (!root["Noise"].empty())
	{
		bHaveSound = true;
		sound = root["Noise"].asInt();
	}
	if (!root["CO2"].empty())
	{
		bHaveCO2 = true;
		co2 = root["CO2"].asInt();
	}
	if (!root["sum_rain_24"].empty())
	{
		bHaveRain = true;
		rain = root["sum_rain_24"].asInt();
	}
	if (!root["WindAngle"].empty())
	{
		if ((!root["WindAngle"].empty()) && (!root["WindStrength"].empty()) && (!root["GustAngle"].empty()) && (!root["GustStrength"].empty()))
		{
			bHaveWind = true;
			wind_angle = root["WindAngle"].asInt();
			wind_strength = root["WindStrength"].asFloat() / 3.6F;
			wind_gust = root["GustStrength"].asFloat() / 3.6F;
		}
	}

	//Data retreived create / update appropriate domoticz devices
	//Temperature and humidity sensors
	if (bHaveTemp && bHaveHum && bHaveBaro)
	{
		int nforecast = m_forecast_calculators[ID].CalculateBaroForecast(temp, baro);
		SendTempHumBaroSensorFloat(ID, batValue, temp, hum, baro, (uint8_t)nforecast, name, rssiLevel);
	}
	else if (bHaveTemp && bHaveHum)
		SendTempHumSensor(ID, batValue, temp, hum, name, rssiLevel);
	else if (bHaveTemp)
		SendTempSensor(ID, batValue, temp, name, rssiLevel);

	//Thermostat device
	if (bHaveSetpoint)
	{
		std::string sName = name + " - SetPoint ";
		SendSetPointSensor((uint8_t)((ID & 0x00FF0000) >> 16), (ID & 0XFF00) >> 8, ID & 0XFF, sp_temp, sName);
	}

	//Rain meter
	if (bHaveRain)
	{
		bool bRefetchData = (m_RainOffset.find(ID) == m_RainOffset.end());
		if (!bRefetchData)
			bRefetchData = ((m_RainOffset[ID] == 0) && (m_OldRainCounter[ID] == 0));
		if (bRefetchData)
		{
			// get last rain counter from the database
			bool bExists = false;
			m_RainOffset[ID] = GetRainSensorValue(ID, bExists);
			m_RainOffset[ID] -= rain;
			if (m_RainOffset[ID] < 0)
				m_RainOffset[ID] = 0;
			if (m_OldRainCounter.find(ID) == m_OldRainCounter.end())
				m_OldRainCounter[ID] = 0;
		}
		// daily counter went to zero ?
		if (rain < m_OldRainCounter[ID])
			m_RainOffset[ID] += m_OldRainCounter[ID];

		m_OldRainCounter[ID] = rain;
		SendRainSensor(ID, batValue, m_RainOffset[ID] + m_OldRainCounter[ID], name, rssiLevel);
	}

	if (bHaveCO2)
		SendAirQualitySensor(ID, DevIdx, batValue, co2, name);

	if (bHaveSound)
		SendSoundSensor(ID, batValue, sound, name);

	if (bHaveWind)
		SendWind(ID, batValue, wind_angle, wind_strength, wind_gust, 0, 0, false, false, name, rssiLevel);

	return true;
}

/// <summary>
/// Parse data for energy station (thermostat and valves) and get
/// module / room and schedule
/// </summary>
/// <param name="sResult">JSON raw data to parse</param>
/// <returns>success parsing the data</returns>
bool CNetatmo::ParseHomeData(const std::string& sResult, Json::Value& root )
{
	//
        bool bHaveHomes = true;
        if (root["body"].empty())
                bHaveHomes = false;
        else if (root["body"]["homes"].empty())
                bHaveHomes = false;

        //Return error if no devices are found
        if (!bHaveHomes)
        {
                Log(LOG_STATUS, "No Homes found...");
                return false;
        }
        //HomesData
        std::vector<_tNetatmoDevice> _netatmo_devices;
        int iDevIndex = 0;
        for (auto home : root["body"]["homes"])
        {
                if (!home["id"].empty())
                {
                        // dict_keys(['id', 'name', 'altitude', 'coordinates', 'country', 'timezone', 'rooms', 'modules', 'temperature_control_mode', 'therm_mode', 'therm_setpoint_default_duration', 'persons', 'schedules'])
                        // 
			Log(LOG_STATUS, "ParsHomeData.");
		}
	}
	//return true;
	return false;
}

/// <summary>
/// Parse data for energy / security devices
/// get and create/update domoticz devices
/// </summary>
/// <param name="sResult">JSON raw data to parse</param>
/// <returns></returns>
bool CNetatmo::ParseHomeStatus(const std::string& sResult, Json::Value& root )
{
	//
	if (root["body"].empty())
		return false;

	if (root["body"]["home"].empty())
		return false;

	//int thermostatID;
	//Parse module and create / update domoticz devices
	if (!root["body"]["home"]["modules"].empty())
	{
		if (!root["body"]["home"]["modules"].isArray())
			return false;
		Json::Value mRoot = root["body"]["home"]["modules"];

		int iModuleIndex = 0;
		for (auto module : mRoot)
		{
			if (!module["id"].empty())
			{
				std::string id = module["id"].asString();
				int moduleID = iModuleIndex;
                                int batteryLevel = 255;
                                iModuleIndex ++;
				std::string type = module["type"].asString();
				// Find the module (name/id)
                                int crcId = Crc32(0, (const unsigned char*)id.c_str(), id.length());
                                m_ModuleIDs[id] = crcId;
                                std::string moduleName = m_ModuleNames[id];
                                //Device to get battery level / network strength 
                                if (!module["battery_level"].empty())
                                {
                                        batteryLevel = module["battery_level"].asInt() / 100;
                                        //Debug(DEBUG_HARDWARE, "ParseBat %d", batteryLevel); //Batterij
                                        //batteryLevel = GetBatteryLevel(module["type"].asString(), batteryLevel);
                                };
                                if (!module["rf_strength"].empty())
                                {
                                        float rf_strength = module["rf_strength"].asFloat();
                                        // 90=low, 60=highest
                                        if (rf_strength > 90.0F)
                                                rf_strength = 90.0F;
                                        if (rf_strength < 60.0F)
                                                rf_strength = 60.0F;

                                        //range is 30
                                        float mrf_percentage = (100.0F / 30.0F) * float((90 - rf_strength));
                                        if (mrf_percentage != 0)
                                        {
                                                std::string pName = " " + moduleName + " - Sig. + Bat. Lvl";
//                                                Debug(DEBUG_HARDWARE, "Parse Name %s - %d", pName.c_str(), moduleID );
                                                SendPercentageSensor(crcId, 1, batteryLevel, mrf_percentage, pName );
                                        }
                                };
                                if (!module["rf_state"].empty())
                                {
                                        //
                                        std::string rf_state = module["rf_state"].asString();
                                        //
                                };

                                if (!module["wifi_state"].empty())
                                {
                                        //
                                        std::string wifi_state = module["wifi_state"].asString();
                                        //
                                };
                                int wifi_status = 0;
                                if (!module["wifi_strength"].empty())
                                {
                                        // 86=bad, 56=good
                                        wifi_status = (86 - module["wifi_strength"].asInt()) / 3;
                                        if (wifi_status > 10)
                                                wifi_status = 10;
                                        //
                                };
                                if (!module["ts"].empty())
                                {
                                        //timestamp
                                        int timestamp = module["ts"].asInt();
                                        //
                                };
                                if (!module["last_seen"].empty())
                                {
                                        //
                                        int last_seen = module["last_seen"].asInt();
                                        //
                                };
                                if (!module["last_activity"].empty())
                                {
                                        //
                                        int last_activity = module["last_activity"].asInt();
                                        //
                                };
                                if (!module["sd_status"].empty())
                                {
                                        //status off SD-card
                                        int sd_status = module["sd_status"].asInt();
                                        //
                                };
                                if (!module["reachable"].empty())
                                {
                                        // True / False
                                };
                                if (!module["alim_status"].empty())
                                {
                                        // Checks the adaptor state
                                };
                                if (!module["vpn_url"].empty())
                                {
                                        // VPN url from camera

                                        //
                                };
                                if (!module["is_local"].empty())
                                {
                                        // Camera is locally connected - True / False

                                        //
                                };
                                if (!module["monitoring"].empty())
                                {
                                        // Camera On / Off

                                        //
                                };
                                if (!module["bridge"].empty())
                                {
                                        std::string bridge_ = module["bridge"].asString();
                                        std::string Bridge_Name = m_ModuleNames[bridge_];
                                        std::string Module_Name = moduleName + " - Bridge";
                                        std::string Bridge_Text = bridge_ + "  " + Bridge_Name;
                                        //SendTextSensor(const int NodeID, const int ChildID, const int BatteryLevel, const std::string& textMessage, const std::string& defaultname)
                                        SendTextSensor(crcId, 1, 255, Bridge_Text, Module_Name);
                                };
                                if (!module["boiler_valve_comfort_boost"].empty())
                                {
                                        std::string boiler_boost = module["boiler_valve_comfort_boost"].asString();
                                        std::string aName = moduleName + " - Boost";
                                        //Debug(DEBUG_HARDWARE, "Boiler Boost %s - %s", aName.c_str(), boiler_boost.c_str() );
                                        bool bIsActive = module["boiler_valve_comfort_boost"].asBool();
                                        SendSwitch(crcId, 0, 255, bIsActive, 0, aName, m_Name);

                                };
				if (!module["boiler_status"].empty())
				{
					//Thermostat status (boiler heating or not : informationnal switch)
					std::string aName = m_ThermostatName + " - Heating Status";
					bool bIsActive = (module["boiler_status"].asString() == "true");
					//
					SendSwitch(moduleID, 0, 255, bIsActive, 0, aName, m_Name);

					//Thermostat schedule switch (actively changing thermostat schedule)
					std::string allSchName = "Off";
					std::string allSchAction = "";
					for (std::map<int, std::string>::const_iterator itt = m_ScheduleNames.begin(); itt != m_ScheduleNames.end(); ++itt)
					{
						allSchName = allSchName + "|" + itt->second;
						std::stringstream ss;
						ss << itt->first;
					}
					//Selected Index for the dropdown list
					std::stringstream ssv;
					ssv << m_selectedScheduleID;
					//create update / domoticz device
					std::string sName = moduleName + " - Schedule";
                                        SendSelectorSwitch(crcId, 2, ssv.str(), sName, 15, true, allSchName, allSchAction, true, m_Name);
				};
                                if (!module["status"].empty())
                                {
                                        // Door sensor
                                        //std::string aName = m_[id];
                                        //;
                                        //
                                        //SendSwitch(crcId, 0, 255, bIsActive, 0, aName, m_Name);
                                };
                                if (!module["floodlight"].empty())
                                {
                                        //Light Outdoor Camera
                                        //      AUTO / ON / OFF
                                        std::string aName = moduleName + " - Light";
                                        Debug(DEBUG_HARDWARE, "Floodlight name %s - %d  - m_Name %s", aName.c_str(), crcId,  m_Name.c_str());
                                        bool bIsActive = (module["floodlight"].asString() == "true"); //
                                        std::string setpoint_mode = (module["floodlight"].asString());
                                        SendSelectorSwitch(crcId, 3, setpoint_mode, aName, 15, true, "Off|On|Auto", "", true, m_Name);
                                        //SendSwitch(crcId, 0, 255, bIsActive, 0, aName, m_Name);
                                };
			}
		}
	}
	//Parse Rooms
	int iDevIndex = 0;
	bool setModeSwitch = false;
	if (!root["body"]["home"]["rooms"].empty())
	{
		if (!root["body"]["home"]["rooms"].isArray())
			return false;
		Json::Value mRoot = root["body"]["home"]["rooms"];

		for (auto room : mRoot)
		{
			if (!room["id"].empty())
			{
				std::string roomNetatmoID = room["id"].asString();
				std::string roomName = roomNetatmoID;
                                int roomID = iDevIndex + 1;
                                iDevIndex ++;
                                int crcId = Crc32(0, (const unsigned char*)roomNetatmoID.c_str(), roomNetatmoID.length());
//                                m_ModuleIDs[roomNetatmoID] = crcId;

				//Find the room name
				roomName = m_RoomNames[roomNetatmoID];
				roomID = m_RoomIDs[roomNetatmoID];
				std::string roomType = m_Room_Type[roomNetatmoID];
                                std::string roomCategory = m_Module_category[roomNetatmoID];

				m_thermostatDeviceID[roomID & 0xFFFFFF] = roomNetatmoID;
				m_thermostatModuleID[roomID & 0xFFFFFF] = roomNetatmoID;

				//Create / update domoticz devices : Temp sensor / Set point sensor for each room
				if (!room["therm_measured_temperature"].empty())
					SendTempSensor(crcId, 255, room["therm_measured_temperature"].asInt(), roomName);
				if (!room["therm_setpoint_temperature"].empty())
					SendSetPointSensor((uint8_t)((crcId & 0x00FF0000) >> 16), (roomID & 0XFF00) >> 8, roomID & 0XFF, room["therm_setpoint_temperature"].asInt(), roomName);

				if (!setModeSwitch)
				{
					// create / update the switch for setting away mode
					// on the themostat (we could create one for each room also,
					// but as this is not something we can do on the app, we don't here)
					std::string setpoint_mode = room["therm_setpoint_mode"].asString();
					if (setpoint_mode == "away")
						setpoint_mode = "20";
					else if (setpoint_mode == "hg")
						setpoint_mode = "30";
					else
						setpoint_mode = "10";
					SendSelectorSwitch(crcId, 3, setpoint_mode, m_ThermostatName + " - Mode", 15, true, "Off|On|Away|Frost Guard", "", true, m_Name);

					setModeSwitch = true;
				}
			}
			iDevIndex++;
		}
	}
        //Parse Persons
        int iPersonIndex = 0;
        if (!root["body"]["home"]["persons"].empty())
        {
                if (!root["body"]["home"]["persons"].isArray())
                        return false;
                Json::Value mRoot = root["body"]["home"]["persons"];

                for (auto person : mRoot)
                {
                        if (!person["id"].empty())
                        // home_persons_ dict_keys(['id', 'last_seen', 'out_of_sight'])
                        {
                                std::string PersonNetatmoID = person["id"].asString();
                                std::string PersonName;
                                int PersonID = iPersonIndex + 1;
                                iPersonIndex ++;
                                //Debug(DEBUG_HARDWARE, "ParseHome Person %d", PersonID);
                                //Find the Person name
                                PersonName = m_PersonsNames[PersonNetatmoID];

                                std::string PersonLastSeen = person["last_seen"].asString();
                                std::string PersonAway = person["out_of_sight"].asString();
                        }
                }
        }
        return (iPersonIndex > 0);
}

/// <summary>
/// Normalize battery level for station module                    //Not Used anymore
/// </summary>
/// <param name="ModuleType">Module type</param>
/// <param name="battery_percent">battery percent</param>
/// <returns>normalized battery level</returns>
int CNetatmo::GetBatteryLevel(const std::string& ModuleType, int battery_percent)
{
	int batValue = 255;

	// Others are plugged
	if ((ModuleType == "NAModule1") || (ModuleType == "NAModule2") || (ModuleType == "NAModule3") || (ModuleType == "NAModule4"))
		batValue = battery_percent;
	else if (ModuleType == "NRV")
	{
		if (battery_percent > 3200)
			battery_percent = 3200;
		else if (battery_percent < 2200)
			battery_percent = 2200;

		// range = 1000
		batValue = 3200 - battery_percent;
		batValue = 100 - int((100.0F / 1000.0F) * float(batValue));
	}
	else if (ModuleType == "NATherm1")
	{
		if (battery_percent > 4100)
			battery_percent = 4100;
		else if (battery_percent < 3000)
			battery_percent = 3000;

		// range = 1100
		batValue = 4100 - battery_percent;
		batValue = 100 - int((100.0F / 1100.0F) * float(batValue));
	}
	return batValue;
}
