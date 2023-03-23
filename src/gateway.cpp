#include "gateway.h"

#include "log.h"

using namespace std;
using namespace WBMQTT;

#define LOG(logger) ::logger.Log() << "[gateway] "

namespace
{
    bool Append(IEC104::TInformationObjects& objs,
                PControl control,
                const std::string& v,
                const TIecInformationObject& obj) noexcept
    {
        if (v.empty()) {
            return false;
        }

        auto now = std::chrono::system_clock::now();

        try {
            switch (obj.Type) {
                case SinglePoint: {
                    if (v == "0") {
                        objs.SinglePoint.emplace_back(obj.Address, false);
                        return true;
                    }
                    if (v == "1") {
                        objs.SinglePoint.emplace_back(obj.Address, true);
                        return true;
                    }
                    throw std::runtime_error(v + " is not convertible to bool");
                }
                case MeasuredValueShort: {
                    objs.MeasuredValueShort.emplace_back(obj.Address, stof(v));
                    return true;
                }
                case MeasuredValueScaled: {
                    objs.MeasuredValueScaled.emplace_back(obj.Address, stoi(v));
                    return true;
                }
                case SinglePointWithTimestamp: {
                    if (v == "0") {
                        objs.SinglePointWithTimestamp.emplace_back(obj.Address, now, false);
                        return true;
                    }
                    if (v == "1") {
                        objs.SinglePointWithTimestamp.emplace_back(obj.Address, now, true);
                        return true;
                    }
                    throw std::runtime_error(v + " is not convertible to bool");
                }
                case MeasuredValueShortWithTimestamp: {
                    objs.MeasuredValueShortWithTimestamp.emplace_back(obj.Address, now, stof(v));
                    return true;
                }
                case MeasuredValueScaledWithTimestamp: {
                    objs.MeasuredValueScaledWithTimestamp.emplace_back(obj.Address, now, stoi(v));
                    return true;
                }
            }
        } catch (const std::exception& e) {
            LOG(Warn) << "'" << control->GetDevice()->GetId() << "'/'" << control->GetId() << "' = '" << v
                      << "' is not convertible to IEC 608760-5-104 information object: " << e.what();
        }
        return false;
    }

    std::string GetFullName(PControl control)
    {
        return "'" + control->GetDevice()->GetId() + "'/'" + control->GetId() + "'";
    }
}

TGateway::TGateway(PDeviceDriver driver, IEC104::IServer* iecServer, const TDeviceConfig& devices)
    : Driver(driver),
      Devices(devices),
      IecServer(iecServer)
{
    std::vector<std::string> deviceIds;
    for (const auto& device: devices) {
        LOG(Debug) << "'" << device.first << "' is added to filter";
        deviceIds.emplace_back(device.first);
    }
    Driver->SetFilter(GetDeviceListFilter(deviceIds));
    Driver->WaitForReady();

    for (const auto& device: devices) {
        for (const auto& control: device.second) {
            IoaToControls[control.second.Address] = {device.first, control.first};
        }
    }

    Driver->On<TControlValueEvent>([this](const WBMQTT::TControlValueEvent& event) { OnValueChanged(event); });
    iecServer->SetHandler(this);
}

void TGateway::Stop()
{
    IecServer->Stop();
    Driver->StopLoop();
}

void TGateway::OnValueChanged(const WBMQTT::TControlValueEvent& event)
{
    auto itDevice = Devices.find(event.Control->GetDevice()->GetId());
    if (itDevice == Devices.end()) {
        LOG(Debug) << "Got message from " << GetFullName(event.Control) << ". No config for device";
        return;
    }

    auto itControl = itDevice->second.equal_range(event.Control->GetId());
    if (itControl.first == itDevice->second.end()) {
        LOG(Debug) << "Got message from " << GetFullName(event.Control) << ". No config for control";
        return;
    }

    bool hasObjs = false;
    IEC104::TInformationObjects objs;
    while (itControl.first != itControl.second) {
        hasObjs |= Append(objs, event.Control, event.RawValue, itControl.first->second);
        ++itControl.first;
    }
    if (hasObjs) {
        IecServer->SendSpontaneous(objs);
    }
}

IEC104::TInformationObjects TGateway::GetInformationObjectsValues() const noexcept
{
    IEC104::TInformationObjects objs;
    try {
        auto tx = Driver->BeginTx();
        for (const auto& device: Devices) {
            auto pDevice = tx->GetDevice(device.first);
            if (pDevice) {
                for (const auto& control: device.second) {
                    auto pControl = pDevice->GetControl(control.first);
                    if (pControl) {
                        Append(objs, pControl, pControl->GetRawValue(), control.second);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG(Warn) << "TGateway::GetInformationObjectsValues() error: " << e.what();
    }
    LOG(Debug) << "TGateway::GetInformationObjectsValues()\n"
               << "\n\tSinglePoint:" << objs.SinglePoint.size()
               << "\n\tMeasuredValueShort:" << objs.MeasuredValueShort.size()
               << "\n\tMeasuredValueScaled:" << objs.MeasuredValueScaled.size()
               << "\n\tSinglePointWithTimestamp:" << objs.SinglePointWithTimestamp.size()
               << "\n\tMeasuredValueShortWithTimestamp:" << objs.MeasuredValueShortWithTimestamp.size()
               << "\n\tMeasuredValueScaledWithTimestamp:" << objs.MeasuredValueScaledWithTimestamp.size();
    return objs;
}

bool TGateway::SetParameter(uint32_t ioa, const std::string& value) noexcept
{
    auto it = IoaToControls.find(ioa);
    if (it == IoaToControls.end()) {
        LOG(Warn) << "Can't find configuration for IOA: " << ioa;
        return false;
    }

    try {
        auto tx = Driver->BeginTx();
        auto pDevice = tx->GetDevice(it->second.Device);
        if (!pDevice) {
            throw std::runtime_error("MQTT broker doesn't have '" + it->second.Device + "' device");
        }
        auto pControl = pDevice->GetControl(it->second.Control);
        if (!pControl) {
            throw std::runtime_error("'" + it->second.Device + "' doesn't contain control '" + it->second.Control +
                                     "'");
        }
        pControl->SetRawValue(tx, value).Sync();
        LOG(Info) << "Set " << GetFullName(pControl) << " = '" << value << "'";
        return true;
    } catch (std::exception& e) {
        LOG(Warn) << "Can't execute setup command IOA: " << ioa << ", value: " << value << ": " << e.what();
    }
    return false;
}
