#include "IEC104Server.h"

#include <stdexcept>
#include <functional>

#include "iec60870_slave.h"
#include "cs104_slave.h"

#include "hal_thread.h"
#include "hal_time.h"

#include "log.h"

#define LOG(logger) ::logger.Log() << "[IEC] "

namespace
{
    class TServerImpl: public IEC104::IServer
    {
        CS104_Slave              Slave;
        CS101_AppLayerParameters AppLayerParameters;
        uint32_t                 CommonAddress;
        IEC104::IHandler*        Handler;
    public:
        TServerImpl(const IEC104::TServerConfig& config);
        ~TServerImpl();

        void Stop();
        void SendSpontaneous(const IEC104::TInformationObjects& objs);
        void SetHandler(IEC104::IHandler* handler);

        bool IsReadyToAcceptConnections() const;
        bool HandleAsdu(IMasterConnection connection, CS101_ASDU asdu);
        void HandleConnectionEvent(IMasterConnection connection, CS104_PeerConnectionEvent event);
        void HandleInterrogationRequest(IMasterConnection connection, CS101_ASDU asdu, int qoi);
    };

    extern "C"
    {
        bool RequestConnectionHandler(void* parameter, const char* ipAddress)
        {
            return ((TServerImpl*)parameter)->IsReadyToAcceptConnections();
        }

        void ConnectionEventHandler(void* parameter, IMasterConnection connection, CS104_PeerConnectionEvent event)
        {
            return ((TServerImpl*)parameter)->HandleConnectionEvent(connection, event);
        }

        bool AsduHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu)
        {
            return ((TServerImpl*)parameter)->HandleAsdu(connection, asdu);
        }

        bool ClockSyncHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu, CP56Time2a newTime)
        {
            // Do nothing. Just for compatibility with OPC servers
            return true;
        }

        bool InterrogationHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu, uint8_t qoi)
        {
            ((TServerImpl*)parameter)->HandleInterrogationRequest(connection, asdu, qoi);
            return true;
        }
    }

    CS101_ASDU Append(InformationObject io,
                      CS101_ASDU asdu,
                      CS101_AppLayerParameters appLayerParameters,
                      int commonAddress,
                      CS101_CauseOfTransmission cot,
                      std::function<void(CS101_ASDU)> sendFn)
    {
        if (!CS101_ASDU_addInformationObject(asdu, io)) {
            sendFn(asdu);
            CS101_ASDU_destroy(asdu);
            asdu = CS101_ASDU_create(appLayerParameters, false, cot, 0, commonAddress, false, false);
            if (!CS101_ASDU_addInformationObject(asdu, io)) {
                LOG(Warn) << "Can't add information object with address " << InformationObject_getObjectAddress(io) << " to ASDU";
            }
        }
        InformationObject_destroy(io);
        return asdu;
    }

    void Send(CS101_AppLayerParameters appLayerParameters,
              int commonAddress,
              CS101_CauseOfTransmission cot,
              const IEC104::TInformationObjects& objs,
              std::function<void(CS101_ASDU)> sendFn)
    {
        CS101_ASDU asdu = CS101_ASDU_create(appLayerParameters, false, cot, 0, commonAddress, false, false);

        for (const auto& val: objs.SinglePoint) {
            asdu = Append((InformationObject)SinglePointInformation_create(NULL, val.first, val.second, IEC60870_QUALITY_GOOD),
                          asdu,
                          appLayerParameters,
                          commonAddress,
                          cot,
                          sendFn);
        }

        for (const auto& val: objs.MeasuredValueShort) {
            asdu = Append((InformationObject)MeasuredValueShort_create(NULL, val.first, val.second, IEC60870_QUALITY_GOOD),
                          asdu,
                          appLayerParameters,
                          commonAddress,
                          cot,
                          sendFn);
        }

        for (const auto& val: objs.MeasuredValueScaled) {
            asdu = Append((InformationObject)MeasuredValueScaled_create(NULL, val.first, val.second, IEC60870_QUALITY_GOOD),
                          asdu,
                          appLayerParameters,
                          commonAddress,
                          cot,
                          sendFn);
        }

        if (CS101_ASDU_getPayloadSize(asdu)) {
            sendFn(asdu);
        }
        CS101_ASDU_destroy(asdu);
    }

    void HandleCommand(CS101_ASDU        asdu,
                       IMasterConnection connection,
                       IEC104::IHandler* handler,
                       std::function<std::string(InformationObject io)> fn)
    {
        if (CS101_ASDU_getCOT(asdu) == CS101_COT_ACTIVATION) {
            InformationObject io = CS101_ASDU_getElement(asdu, 0);
            CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_CON);
            if (!handler->SetParameter(InformationObject_getObjectAddress(io), fn(io))) {
                CS101_ASDU_setNegative(asdu, true);
            }
            InformationObject_destroy(io);
        } else {
            CS101_ASDU_setCOT(asdu, CS101_COT_UNKNOWN_COT);
        }
        IMasterConnection_sendASDU(connection, asdu);
    }

    TServerImpl::TServerImpl(const IEC104::TServerConfig& config)
        : CommonAddress(config.CommonAddress), Handler(nullptr)
    {
        Slave = CS104_Slave_create(100, 100);

        CS104_Slave_setLocalAddress(Slave, config.BindIp.empty() ? "0.0.0.0" : config.BindIp.c_str());

        AppLayerParameters = CS104_Slave_getAppLayerParameters(Slave);

        CS104_Slave_setConnectionRequestHandler(Slave, RequestConnectionHandler, this);
        CS104_Slave_setConnectionEventHandler(Slave, ConnectionEventHandler, this);
        CS104_Slave_setASDUHandler(Slave, AsduHandler, this);
        CS104_Slave_setClockSyncHandler(Slave, ClockSyncHandler, NULL);
        CS104_Slave_setInterrogationHandler(Slave, InterrogationHandler, this);

        // Set server mode to allow multiple clients using the application layer
        CS104_Slave_setServerMode(Slave, CS104_MODE_CONNECTION_IS_REDUNDANCY_GROUP);

        CS104_Slave_start(Slave);

        if (CS104_Slave_isRunning(Slave) == false) {
            CS104_Slave_destroy(Slave);
            throw std::runtime_error("starting IEC 60870-5-104 server failed");
        }
    }

    TServerImpl::~TServerImpl()
    {
        Stop();
        CS104_Slave_destroy(Slave);
    }

    void TServerImpl::Stop()
    {
        if (CS104_Slave_isRunning(Slave) == true) {
            CS104_Slave_stop(Slave);
        }
    }

    void TServerImpl::SendSpontaneous(const IEC104::TInformationObjects& objs)
    {
        if (CS104_Slave_isRunning(Slave) == false) {
            throw std::runtime_error("IEC 60870-5-104 is not running");
        }

        Send(AppLayerParameters, CommonAddress, CS101_COT_SPONTANEOUS, objs, [&](CS101_ASDU asdu) {CS104_Slave_enqueueASDU(Slave, asdu);});
    }

    bool TServerImpl::IsReadyToAcceptConnections() const
    {
        return (Handler != nullptr);
    }

    void TServerImpl::SetHandler(IEC104::IHandler* handler)
    {
        if (Handler != nullptr) {
            throw std::runtime_error("IIEC104Handler can be set only once");
        }
        Handler = handler;
    }

    bool TServerImpl::HandleAsdu(IMasterConnection connection, CS101_ASDU asdu)
    {
        if (!Handler) {
            LOG(Debug) << "Got ASDU, no handler";
            return false;
        }
        auto asduType = CS101_ASDU_getTypeID(asdu);
        if (Debug.IsEnabled()) {
            LOG(Debug) << "Got ASDU: " << TypeID_toString(asduType)
                       << ", COT: " << CS101_CauseOfTransmission_toString(CS101_ASDU_getCOT(asdu));
        }
        switch (asduType)
        {
        case C_SC_NA_1: // Single command
            HandleCommand(asdu, connection, Handler,
                            [](InformationObject io)
                            {
                                return (SingleCommand_getState((SingleCommand)io) ? "1" : "0");
                            });
            return true;
        case C_SE_NB_1: // Measured value scaled command
            HandleCommand(asdu, connection, Handler,
                            [](InformationObject io)
                            {
                                return std::to_string(MeasuredValueScaled_getValue((MeasuredValueScaled)io));
                            });
            return true;
        case C_SE_NC_1: // Measured value short command
            HandleCommand(asdu, connection, Handler,
                            [](InformationObject io)
                            {
                                return std::to_string(MeasuredValueShort_getValue((MeasuredValueShort)io));
                            });
            return true;
        default:
            break;
        }
        return false;
    }

    void TServerImpl::HandleConnectionEvent(IMasterConnection connection, CS104_PeerConnectionEvent event)
    {
        char addrBuf[24] = {0};
        IMasterConnection_getPeerAddress(connection, addrBuf, sizeof(addrBuf) - 1);
        switch (event)
        {
            case CS104_CON_EVENT_CONNECTION_OPENED: LOG(Info) << "Connection opened " << addrBuf; break;
            case CS104_CON_EVENT_CONNECTION_CLOSED: LOG(Info) << "Connection closed " << addrBuf; break;
            case CS104_CON_EVENT_DEACTIVATED:       LOG(Info) << "Connection deactivated " << addrBuf; break;
            case CS104_CON_EVENT_ACTIVATED: {
                LOG(Info) << "Connection activated " << addrBuf;
                Send(AppLayerParameters,
                     CommonAddress,
                     CS101_COT_SPONTANEOUS,
                     Handler->GetInformationObjectsValues(),
                     [&](CS101_ASDU asdu) {CS104_Slave_enqueueASDU(Slave, asdu);});
                break;
            }
        }
    }

    void TServerImpl::HandleInterrogationRequest(IMasterConnection connection, CS101_ASDU incomimgAsdu, int qoi)
    {
        if (qoi == 20) { /* only handle station interrogation */
            IMasterConnection_sendACT_CON(connection, incomimgAsdu, false);

            Send(AppLayerParameters,
                CommonAddress,
                CS101_COT_INTERROGATED_BY_STATION,
                Handler->GetInformationObjectsValues(),
                [&](CS101_ASDU asdu) {IMasterConnection_sendASDU(connection, asdu);});

            IMasterConnection_sendACT_TERM(connection, incomimgAsdu);
        }
        else {
            char addrBuf[24] = {0};
            IMasterConnection_getPeerAddress(connection, addrBuf, sizeof(addrBuf) - 1);
            LOG(Warn) << addrBuf << " unsupported interrogation qoi=" << qoi;
            IMasterConnection_sendACT_CON(connection, incomimgAsdu, true);
        }
    }
}

std::unique_ptr<IEC104::IServer> IEC104::MakeServer(const IEC104::TServerConfig& config)
{
    return std::unique_ptr<IEC104::IServer>(new TServerImpl(config));
}
