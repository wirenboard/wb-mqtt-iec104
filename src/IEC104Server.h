#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace IEC104
{
    //! IEC104 server configuration parameters
    struct TServerConfig
    {
        //! Local IP to bind the server. If empty, the server will listen to all available local IP's
        std::string BindIp;

        //! Port to listen
        uint16_t BindPort;

        //! IEC common address
        uint32_t CommonAddress;
    };

    template<class T> struct TInformationObject
    {
        uint32_t Address;
        T Value;

        TInformationObject(uint32_t address, T value): Address(address), Value(value)
        {}
    };

    template<class T> struct TInformationObjectWithTimestamp: public TInformationObject<T>
    {
        //! UTC timestamp
        std::chrono::system_clock::time_point Timestamp;

        TInformationObjectWithTimestamp(uint32_t address,
                                        const std::chrono::system_clock::time_point& timestamp,
                                        T value)
            : TInformationObject<T>(address, value),
              Timestamp(timestamp)
        {}
    };

    typedef TInformationObject<bool> TSinglePointInformationObject;
    typedef TInformationObject<float> TMeasuredValueShortInformationObject;
    typedef TInformationObject<int> TMeasuredValueScaledInformationObject;

    typedef TInformationObjectWithTimestamp<bool> TSinglePointInformationObjectWithTimestamp;
    typedef TInformationObjectWithTimestamp<float> TMeasuredValueShortInformationObjectWithTimestamp;
    typedef TInformationObjectWithTimestamp<int> TMeasuredValueScaledInformationObjectWithTimestamp;

    //! IEC information objects. Vectors of pairs "information object address"-"value"
    struct TInformationObjects
    {
        std::vector<TSinglePointInformationObject> SinglePoint;
        std::vector<TMeasuredValueShortInformationObject> MeasuredValueShort;
        std::vector<TMeasuredValueScaledInformationObject> MeasuredValueScaled;
        std::vector<TSinglePointInformationObjectWithTimestamp> SinglePointWithTimestamp;
        std::vector<TMeasuredValueShortInformationObjectWithTimestamp> MeasuredValueShortWithTimestamp;
        std::vector<TMeasuredValueScaledInformationObjectWithTimestamp> MeasuredValueScaledWithTimestamp;
    };

    //! Interface of external event handler
    class IHandler
    {
    public:
        virtual ~IHandler() = default;

        //! Return values of all information objects. Must be threadsafe.
        virtual TInformationObjects GetInformationObjectsValues() const noexcept = 0;

        /**
         * @brief Process value received with IEC command.
         *        Must be threadsafe.
         *
         * @param ioa information object address of command
         * @param value value received from command
         * @return true - received value successfully processed by handler. Positive acknowledgement to command will be
         * send
         * @return false - an error occurred during processing. Negative response to command will be send
         */
        virtual bool SetParameter(uint32_t ioa, const std::string& value) noexcept = 0;
    };

    //! Interface of IEC104 server. Note that in IEC terms a server is a controlling unit (slave)
    class IServer
    {
    public:
        virtual ~IServer() = default;

        //! Stop the server. Must be threadsafe.
        virtual void Stop() = 0;

        /**
         * @brief Send messages with spontaneous cause of transmission (3).
         *        Must be threadsafe.
         *
         * @param obj information objects to send
         */
        virtual void SendSpontaneous(const TInformationObjects& obj) = 0;

        /**
         * @brief Set the Handler object for commands. The server doesn't own handler object.
         *        Handler object must be available during all lifetime of the server.
         *        Handler object can be set only once.
         */
        virtual void SetHandler(IHandler* handler) = 0;
    };

    //! Make a new instance of server
    std::unique_ptr<IServer> MakeServer(const TServerConfig& config);
}
