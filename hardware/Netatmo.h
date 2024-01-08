#pragma once

#include "DomoticzHardware.h"
#include "../main/BaroForecastCalculator.h"

namespace Json
{
	class Value;
} // namespace Json

class CNetatmo : public CDomoticzHardwareBase
{
      //
      private:
	enum _eNetatmoType
	{
		NETYPE_WEATHER_STATION = 0,
		NETYPE_MEASURE,
		NETYPE_HOMECOACH,
		NETYPE_THERMOSTAT,
		NETYPE_HOME,
                NETYPE_HOMESDATA,
		NETYPE_STATUS,
		NETYPE_CAMERAS,
		NETYPE_EVENTS
	};
        Json::Value m_root;
	std::string m_clientId;
	std::string m_clientSecret;
	std::string m_scopes;
	std::string m_redirectUri;
	std::string m_authCode;
	std::string m_username;
	std::string m_password;
	std::string m_accessToken;
	std::string m_refreshToken;
	std::map<int, std::string> m_thermostatDeviceID;
	std::map<int, std::string> m_thermostatModuleID;
	bool m_bPollWeatherData;
	bool m_bPollHomecoachData;
	bool m_bPollHomeData;
        bool m_bPollHomesData;
	bool m_bPollHomeStatus;

	//bool m_bPollMeasureData;
	bool m_bPollThermostat;
        bool m_bFirstTimeThermostat;
	bool m_bFirstTimeWeatherData;
	bool m_bForceSetpointUpdate;
	time_t m_tSetpointUpdateTime;

	std::shared_ptr<std::thread> m_thread;

	time_t m_nextRefreshTs;

	std::map<int, float> m_RainOffset;
	std::map<int, float> m_OldRainCounter;

	std::map<int, bool> m_bNetatmoRefreshed;

	void Init();
	bool StartHardware() override;
	bool StopHardware() override;
	void Do_Work();

	std::string MakeRequestURL(_eNetatmoType NetatmoType);

        void GetWeatherDetails();
	void GetHomecoachDetails();
        bool GetHomeDetails();
	void GetHomesDataDetails();
	void GetHomeStatusDetails();
	void GetThermostatDetails();

        void Get_Measure();
        void Get_Picture();
        void Get_Events();

        bool ParseStationData(const std::string &sResult, bool bIsThermostat);
	bool ParseHomeData(const std::string &sResult);
	bool ParseHomeStatus(const std::string &sResult);

	bool SetAway(int idx, bool bIsAway);
	bool SetSchedule(int scheduleId);

	bool Login();
	bool RefreshToken(bool bForce = false);
	bool LoadRefreshToken();
	void StoreRefreshToken();
	bool m_isLogged;
	bool m_bForceLogin;
	
	_eNetatmoType m_measureType;
	_eNetatmoType m_energyType;
	_eNetatmoType m_weatherType;
	_eNetatmoType m_homecoachType;
	_eNetatmoType m_homeType;
	_eNetatmoType m_dataType;
	_eNetatmoType m_statusType;
	_eNetatmoType m_camerasType;
	_eNetatmoType m_eventsType;

	int m_ActHome;
	std::string m_Home_ID;
        std::string m_Home_name;
        std::string m_Place;

        std::map<std::string, std::string> m_Camera_Name;
        std::map<std::string, std::string> m_Camera_ID;
        std::map<std::string, std::string> m_Smoke_Name;
        std::map<std::string, std::string> m_Smoke_ID;

        std::map<std::string, std::string> m_ThermostatName;
	std::map<std::string, std::string> m_RoomNames;
	std::map<std::string, int> m_RoomIDs;
        std::map<std::string, std::string> m_Room;
	std::map<std::string, std::string> m_ModuleNames;
	std::map<std::string, int> m_ModuleIDs;
        std::map<std::string, std::string> m_Persons;
        std::map<std::string, std::string> m_PersonsNames;
        std::map<std::string, int> m_PersonsIDs;
        std::map<std::string, std::string> m_PersonUrl;
	std::map<int, std::string> m_ScheduleNames;
	std::map<int, std::string> m_ScheduleIDs;
	int m_selectedScheduleID;
        std::map<int, std::string> m_ZoneNames;
        std::map<int, std::string> m_ZoneIDs;
        std::map<int, std::string> m_ZoneTypes;

	std::map<int, CBaroForecastCalculator> m_forecast_calculators;

	int GetBatteryLevel(const std::string &ModuleType, int battery_percent);
	bool ParseDashboard(const Json::Value &root, int DevIdx, int ID, const std::string &name, const std::string &ModuleType, int battery_percent, int rf_status);
      public:
        CNetatmo(int ID, const std::string &username, const std::string &password);
        ~CNetatmo() override = default;
        //
        bool WriteToHardware(const char *, unsigned char) override;
        void SetSetpoint(int idx, float temp);
        bool SetProgramState(int idx, int newState);

        void Get_Respons_API(const _eNetatmoType& NType, std::string& sResult, std::string& home_id , bool& bRet, Json::Value& root );
        //
};
