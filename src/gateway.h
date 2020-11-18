#pragma once

#include <wblib/wbmqtt.h>
#include "IEC104Server.h"

enum TIecInformationObjectType
{
    SinglePoint,              //! Single point
    MeasuredValueShort,       //! Measured value short (float)
    MeasuredValueScaled,      //! Measured value scaled (16-bit signed integer)
    MeasuredValueScaledR,     //! Measured value scaled (16-bit signed integer), R component of "rgb" MQTT control
    MeasuredValueScaledG,     //! Measured value scaled (16-bit signed integer), G component of "rgb" MQTT control
    MeasuredValueScaledB      //! Measured value scaled (16-bit signed integer), B component of "rgb" MQTT control
};

struct TIecInformationObject
{
    uint32_t                  Address; //! Information object address
    TIecInformationObjectType Type;
};

// Maps MQTT control name(id) to IEC 60870-5-104 information object address
typedef std::multimap<std::string, TIecInformationObject> TControlsConfig;

// Maps MQTT device name(id) to TControlsConfig
typedef std::map<std::string, TControlsConfig> TDeviceConfig;

struct TControlDesc
{
    enum ParsingHint
    {
        RGBValueR = 0, //! Payload is "R;G;B" list. Take R value
        RGBValueG,     //! Payload is "R;G;B" list. Take G value
        RGBValueB,     //! Payload is "R;G;B" list. Take B value
        FullValue      //! Take full payload
    };

    std::string Device;  //! MQTT device name /devices/XXXX
    std::string Control; //! MQTT control name /devices/+/controls/XXXX
    ParsingHint Hint;    //! Hint used during MQTT payload parsing
};

class TGateway: public IEC104::IHandler
{
        WBMQTT::PDeviceDriver            Driver;
        TDeviceConfig                    Devices;
        IEC104::IServer*                 IecServer;
        std::map<uint32_t, TControlDesc> IoaToControls; // Maps information object address to MQTT control

        //! MQTT value changing handler
        void OnValueChanged(const WBMQTT::TControlValueEvent& event);
    public:
        TGateway(WBMQTT::PDeviceDriver driver, IEC104::IServer* iecServer, const TDeviceConfig& devices);

        //! Stop the server
        void Stop();

        // IEC104::IHandler implementation
        IEC104::TInformationObjects GetInformationObjectsValues() const noexcept;
        bool SetParameter(uint32_t ioa, const std::string& value) noexcept;
};
