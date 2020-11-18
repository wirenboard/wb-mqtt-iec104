#pragma once

#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace IEC104
{
    //! IEC104 server configuration parameters
    struct TServerConfig
    {
        //! Local IP to bind the server. If empty, the server will listen to all available local IP's 
        std::string BindIp;

        //! Port to listen
        uint16_t    BindPort;

        //! IEC common address
        uint32_t    CommonAddress;
    };

    //! IEC information objects. Vectors of pairs "information object address"-"value"
    struct TInformationObjects
    {
        std::vector<std::pair<uint32_t, bool>>  SinglePoint;
        std::vector<std::pair<uint32_t, float>> MeasuredValueShort;
        std::vector<std::pair<uint32_t, int>>   MeasuredValueScaled;
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
         * @param value value recieved from command
         * @return true - received value successfully processed by handler. Positive acknowledgement to command will be send
         * @return false - an error occured during processing. Negative response to command will be send
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
