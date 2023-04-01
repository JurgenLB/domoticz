#include "stdafx.h"
#include "VisualCrossing.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "../httpclient/UrlEncode.h"
#include "hardwaretypes.h"
#include "../main/localtime_r.h"
#include "../httpclient/HTTPClient.h"
#include "../main/json_helper.h"
#include "../main/RFXtrx.h"
#include "../main/mainworker.h"

#define round(a) ( int ) ( a + .5 )

#ifdef _DEBUG
//#define DEBUG_VisualCrossingR
//#define DEBUG_VisualCrossingW
#endif

#ifdef DEBUG_VisualCrossingW
void SaveString2Disk(std::string str, std::string filename)
{
	FILE *fOut = fopen(filename.c_str(), "wb+");
	if (fOut)
	{
		fwrite(str.c_str(), 1, str.size(), fOut);
		fclose(fOut);
	}
}
#endif
#ifdef DEBUG_VisualCrossingR
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

CVisualCrossing::CVisualCrossing(const int ID, const std::string &APIKey, const std::string &Location) :
m_APIKey(APIKey),
m_Location(Location)
{
	m_HwdID = ID;
	Init();
}

void CVisualCrossing::Init()
{
}

bool CVisualCrossing::StartHardware()
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

bool CVisualCrossing::StopHardware()
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

void CVisualCrossing::Do_Work()
{
	Log(LOG_STATUS, "Started...");

	int sec_counter = 290;
	while (!IsStopRequested(1000))
	{
		sec_counter++;
		if (sec_counter % 12 == 0) {
			m_LastHeartbeat = mytime(nullptr);
		}
		if (sec_counter % 300 == 0)
		{
			GetMeterDetails();
		}
	}
	Log(LOG_STATUS,"Worker stopped...");
}

bool CVisualCrossing::WriteToHardware(const char* /*pdata*/, const unsigned char /*length*/)
{
	return false;
}

std::string CVisualCrossing::GetForecastURL()
{
	std::stringstream sURL;
	std::string szLoc = CURLEncode::URLEncode(m_Location);
	sURL << "https://visualcrossing.com/weather-forecast/" << szLoc << "/metric";
	return sURL.str();
}

void CVisualCrossing::GetMeterDetails()
{
	std::string sResult;
#ifdef DEBUG_VisualCrossingR
	sResult = ReadFile("E:\\VisualCrossing.json");
#else
	std::stringstream sURL;
	std::string szLoc = m_Location;
	sURL << "https://weather.visualcrossing.com/VisualCrossingWebServices/rest/services/timeline/" << szLoc << "?key=" << m_APIKey << "&unitGroup=metric&include=current";
	try
	{
		if (!HTTPClient::GET(sURL.str(), sResult))
		{
			Log(LOG_ERROR, "Error getting http data!.");
			return;
		}
	}
	catch (...)
	{
		Log(LOG_ERROR, "Error getting http data!");
		return;
	}
#ifdef DEBUG_VisualCrossingW
	SaveString2Disk(sResult, "E:\\VisualCrossing.json");
#endif

#endif
	Json::Value root;

	bool ret = ParseJSon(sResult, root);
	if ((!ret) || (!root.isObject()))
	{
		Log(LOG_ERROR,"Invalid data received! Check Location, use a City or GPS Coordinates (xx.yyyy,xx.yyyyy)");
		return;
	}
	if (root["currentConditions"].empty() == true)
	{
		Log(LOG_ERROR,"Invalid data received, or unknown location!");
		return;
	}
	/*
	std::string tmpstr2 = root.toStyledString();
	FILE *fOut = fopen("E:\\VisualCrossing.json", "wb+");
	fwrite(tmpstr2.c_str(), 1, tmpstr2.size(), fOut);
	fclose(fOut);
	*/

	float temp;
	int humidity = 0;
	int barometric = 0;
	int barometric_forcast = baroForecastNoInfo;

	temp = root["currentConditions"]["temp"].asFloat();
	
	if (root["currentConditions"]["humidity"].empty() == false)
	{
		humidity = round(root["currentConditions"]["humidity"].asFloat());
	}
	if (root["currentConditions"]["pressure"].empty() == false)
	{
		barometric = atoi(root["currentConditions"]["pressure"].asString().c_str());
		if (barometric<1000)
			barometric_forcast = baroForecastRain;
		else if (barometric<1020)
			barometric_forcast = baroForecastCloudy;
		else if (barometric<1030)
			barometric_forcast = baroForecastPartlyCloudy;
		else
			barometric_forcast = baroForecastSunny;

		if (root["currentConditions"]["icon"].empty() == false)
		{
			std::string forcasticon = root["currentConditions"]["icon"].asString();
			if ((forcasticon == "partly-cloudy-day")||(forcasticon == "partly-cloudy-night"))
			{
				barometric_forcast = baroForecastPartlyCloudy;
			}
			else if (forcasticon == "cloudy")
			{
				barometric_forcast = baroForecastCloudy;
			}
			else if ((forcasticon == "clear-day")||(forcasticon == "clear-night"))
			{
				barometric_forcast = baroForecastSunny;
			}
			else if ((forcasticon == "rain")||(forcasticon == "snow"))
			{
				barometric_forcast = baroForecastRain;
			}
		}
	}

	if (barometric != 0)
	{
		//Add temp+hum+baro device
		SendTempHumBaroSensor(1, 255, temp, humidity, static_cast<float>(barometric), barometric_forcast, "THB");
	}
	else if (humidity != 0)
	{
		//add temp+hum device
		SendTempHumSensor(1, 255, temp, humidity, "TempHum");
	}
	else
	{
		//add temp device
		SendTempSensor(1, 255, temp, "Temperature");
	}

	//Wind
	int wind_degrees = -1;
	float windspeed_ms = 0;
	float windgust_ms = 0;
	float wind_temp = temp;
	float wind_chill = temp;
	//int windgust = 1;
	//float windchill = -1;

	if (root["currentConditions"]["winddir"].empty() == false)
	{
		wind_degrees = atoi(root["currentConditions"]["winddir"].asString().c_str());
	}
	if (root["currentConditions"]["windspeed"].empty() == false)
	{
		if ((root["currentConditions"]["windspeed"] != "N/A") && (root["currentConditions"]["windspeed"] != "--"))
		{
			float temp_wind_kph = static_cast<float>(atof(root["currentConditions"]["windspeed"].asString().c_str()));
			if (temp_wind_kph != -9999.00F)
			{
				//convert to m/s
				windspeed_ms = temp_wind_kph * 0.277777778F;
			}
		}
	}
	if (root["currentConditions"]["windgust"].empty() == false)
	{
		if ((root["currentConditions"]["windgust"] != "N/A") && (root["currentConditions"]["windgust"] != "--"))
		{
			float temp_wind_gust_kph = static_cast<float>(atof(root["currentConditions"]["windgust"].asString().c_str()));
			if (temp_wind_gust_kph != -9999.00F)
			{
				//convert to m/s
				windgust_ms = temp_wind_gust_kph * 0.277777778F;
			}
		}
	}
	if (root["currentConditions"]["feelslike"].empty() == false)
	{
		if ((root["currentConditions"]["feelslike"] != "N/A") && (root["currentConditions"]["feelslike"] != "--"))
		{
			wind_chill = static_cast<float>(atof(root["currentConditions"]["feelslike"].asString().c_str()));
		}
	}
	if (wind_degrees != -1)
	{
		RBUF tsen;
		memset(&tsen,0,sizeof(RBUF));
		tsen.WIND.packetlength = sizeof(tsen.WIND)-1;
		tsen.WIND.packettype = pTypeWIND;
		tsen.WIND.subtype = sTypeWIND4;
		tsen.WIND.battery_level = 9;
		tsen.WIND.rssi = 12;
		tsen.WIND.id1 = 0;
		tsen.WIND.id2 = 1;

		float winddir = float(wind_degrees);
		int aw = round(winddir);
		tsen.WIND.directionh = (BYTE)(aw/256);
		aw -= (tsen.WIND.directionh*256);
		tsen.WIND.directionl = (BYTE)(aw);

		tsen.WIND.av_speedh = 0;
		tsen.WIND.av_speedl = 0;
		int sw = round(windspeed_ms * 10.0F);
		tsen.WIND.av_speedh = (BYTE)(sw/256);
		sw -= (tsen.WIND.av_speedh*256);
		tsen.WIND.av_speedl = (BYTE)(sw);

		tsen.WIND.gusth = 0;
		tsen.WIND.gustl = 0;
		int gw = round(windgust_ms * 10.0F);
		tsen.WIND.gusth = (BYTE)(gw/256);
		gw -= (tsen.WIND.gusth*256);
		tsen.WIND.gustl = (BYTE)(gw);

		//this is not correct, why no wind temperature? and only chill?
		tsen.WIND.chillh = 0;
		tsen.WIND.chilll = 0;
		tsen.WIND.temperatureh = 0;
		tsen.WIND.temperaturel = 0;

		tsen.WIND.tempsign = (wind_temp >= 0)?0:1;
		int at10 = round(std::abs(wind_temp * 10.0F));
		tsen.WIND.temperatureh = (BYTE)(at10/256);
		at10 -= (tsen.WIND.temperatureh*256);
		tsen.WIND.temperaturel = (BYTE)(at10);

		tsen.WIND.chillsign = (wind_chill >= 0)?0:1;
		at10 = round(std::abs(wind_chill * 10.0F));
		tsen.WIND.chillh = (BYTE)(at10/256);
		at10 -= (tsen.WIND.chillh*256);
		tsen.WIND.chilll = (BYTE)(at10);

		sDecodeRXMessage(this, (const unsigned char *)&tsen.WIND, nullptr, 255, nullptr);
	}

	//UV
	float UV = 0;
	if (root["currentConditions"]["uvindex"].empty() == false)
	{
		if ((root["currentConditions"]["uvindex"] != "N/A") && (root["currentConditions"]["uvindex"] != "--"))
		{
			UV = root["currentConditions"]["uvindex"].asFloat();
		}
	}
	if ((UV < 16) && (UV >= 0))
	{
		SendUVSensor(0, 1, 255, UV, "UV Index");
	}

	//Rain
	float rainrateph = 0.0F;
	if (root["currentConditions"]["precip"].empty() == false)
	{
		float precip = root["currentConditions"]["precip"].asFloat();
		rainrateph += precip;
	}
	if (root["currentConditions"]["snow"].empty() == false)
	{
		float snow = root["currentConditions"]["snow"].asFloat();
		rainrateph += snow;
	}
	if (rainrateph >= 0.0F) {
		SendRainRateSensor(1, 255, rainrateph, "Rain");
	}

	//Visibility
	if (root["currentConditions"]["visibility"].empty() == false)
	{
		if ((root["currentConditions"]["visibility"] != "N/A") && (root["currentConditions"]["visibility"] != "--"))
		{
			float visibility = root["currentConditions"]["visibility"].asFloat();
			if (visibility >= 0)
			{
				_tGeneralDevice gdevice;
				gdevice.subtype = sTypeVisibility;
				gdevice.floatval1 = visibility;
				sDecodeRXMessage(this, (const unsigned char *)&gdevice, nullptr, 255, nullptr);
			}
		}
	}
	//Solar Radiation
	float radiation = 0;
	if (root["currentConditions"]["solarradiation"].empty() == false)
	{
		if ((root["currentConditions"]["solarradiation"] != "N/A") && (root["currentConditions"]["solarradiation"] != "--"))
		{
			radiation = root["currentConditions"]["solarradiation"].asFloat();
		}
	}
	if (radiation >= 0.0F)
	{
		SendCustomSensor(2, 0, 255, radiation, "Solar radiation Sensor", "W/m2"); //Device id 2, because of switching from DarkSky results in using the Ozon sensor
	}

	//Cloud Cover
	float cloudcover = 0;
	if (root["currentConditions"]["cloudcover"].empty() == false)
	{
		if ((root["currentConditions"]["cloudcover"] != "N/A") && (root["currentConditions"]["cloudcover"] != "--"))
		{
			cloudcover = root["currentConditions"]["cloudcover"].asFloat();
		}
	}
	if (cloudcover >= 0.0F)
	{
		SendPercentageSensor(1, 0, 255, cloudcover, "Cloud Cover");
	}
}

