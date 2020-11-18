#include "gateway.h"

#include "log.h"

using namespace std;
using namespace WBMQTT;

#define LOG(logger) ::logger.Log() << "[gateway] "

namespace
{
    int GetRgbComponent(const std::string& value, size_t pos)
    {
        auto params = StringSplit(value, ';');
        if (params.size() != 3) {
            throw std::runtime_error("rgb value must have 3 components");
        }
        return stoi(params[pos]);
    }

    bool Append(IEC104::TInformationObjects& objs, PControl control, const std::string& v, const TIecInformationObject& obj) noexcept
    {
        if (v.empty()) {
            return false;
        }

        try {
            switch (obj.Type)
            {
            case SinglePoint:
                if (v == "0") {
                    objs.SinglePoint.emplace_back(std::make_pair(obj.Address, false));
                    return true;
                }
                if (v == "1") {
                    objs.SinglePoint.emplace_back(std::make_pair(obj.Address, true));
                    return true;
                }
                throw std::runtime_error(v + " is not convertible to bool");
            case MeasuredValueShort:
                objs.MeasuredValueShort.emplace_back(std::make_pair(obj.Address, stof(v)));
                return true;
            case MeasuredValueScaled:
                objs.MeasuredValueScaled.emplace_back(std::make_pair(obj.Address, stoi(v)));
                return true;
            case MeasuredValueScaledR:
                objs.MeasuredValueScaled.emplace_back(std::make_pair(obj.Address, GetRgbComponent(v, 0)));
                return true;
            case MeasuredValueScaledG:
                objs.MeasuredValueScaled.emplace_back(std::make_pair(obj.Address, GetRgbComponent(v, 1)));
                return true;
            case MeasuredValueScaledB:
                objs.MeasuredValueScaled.emplace_back(std::make_pair(obj.Address, GetRgbComponent(v, 2)));
                return true;
            }
        } catch (const std::exception& e) {
            LOG(Warn) << "'" << control->GetDevice()->GetId()
                    << "'/'" << control->GetId()
                    << "' = '" << v 
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

    auto tx = driver->BeginTx();
    for (const auto& device: devices) {
        auto pDevice = tx->GetDevice(device.first);
        if (pDevice) {
            for (const auto& control: device.second) {
                auto pControl = pDevice->GetControl(control.first);
                if (pControl) {
                    switch (control.second.Type)
                    {
                    case MeasuredValueScaledR:
                        IoaToControls[control.second.Address] = {device.first, control.first, TControlDesc::RGBValueR};
                        break;
                    case MeasuredValueScaledG:
                        IoaToControls[control.second.Address] = {device.first, control.first, TControlDesc::RGBValueG};
                        break;
                    case MeasuredValueScaledB:
                        IoaToControls[control.second.Address] = {device.first, control.first, TControlDesc::RGBValueB};
                        break;
                    default:
                        IoaToControls[control.second.Address] = {device.first, control.first, TControlDesc::FullValue};
                        break;
                    }
                }
            }
        }
    }
    tx->End();

    Driver->On<TControlValueEvent>([this](const WBMQTT::TControlValueEvent& event){ OnValueChanged(event); });
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
    while (itControl.first != itControl.second)
    {
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
               << "\n\tMeasuredValueScaled:" << objs.MeasuredValueScaled.size();
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
            throw std::runtime_error("MQTT brocker doesn't have '" + it->second.Device + "' device");
        }
        auto pControl = pDevice->GetControl(it->second.Control);
        if (!pControl) {
            throw std::runtime_error("'" + it->second.Device + "' doesn't contain control '" + it->second.Control + "'");
        }
        std::string newValue(value);
        if (it->second.Hint != TControlDesc::FullValue) {
            auto channels = StringSplit(pControl->GetRawValue(), ';');
            if (channels.size() != 3) {
                channels = std::vector<std::string>{"0", "0", "0"};
            }
            channels[it->second.Hint] = value;
            newValue = channels[0] + ";" + channels[1] + ";" + channels[2];
        }
        pControl->SetRawValue(tx, newValue).Sync();
        LOG(Info) << "Set " << GetFullName(pControl) << " = '" << newValue << "'";
        return true;
    } catch (std::exception& e) {
        LOG(Warn) << "Can't execute setup command IOA: " << ioa << ", value: " << value << ": " << e.what();
    }
    return false;
}
