#pragma once
#include "InputActions.h"
#include <GameInput.h>
#include <windows.h>
#include <wrl/client.h>
#include <filesystem>
#include <mutex>
#include <atomic>
namespace NativeInput=GameInput::v3;
struct GamepadSample
{
    Racing2D::Point analog;
    double brake=0;
    std::vector<InputActions::Edge> edges;
    uint64_t reading=0,serial=0,sampled=0,droppedEdges=0,generation=0,callbackErrors=0;
    uint16_t device=0;
    bool connected=false,baseline=true;
};
class GamepadInput
{
public:
    ~GamepadInput();
    bool Initialize(const std::filesystem::path& directory);
    void Stop();
    void SetForeground(bool value);
    GamepadSample Sample();
    uint64_t Now() const { return input_ ? input_->GetCurrentTimestamp() : 0; }
    std::string Status() const;
    bool InputLossDetected() const;
    bool Available() const { return input_!=nullptr; }
private:
    static void CALLBACK DeviceCallback(NativeInput::GameInputCallbackToken,void*,NativeInput::IGameInputDevice*,uint64_t,NativeInput::GameInputDeviceStatus,NativeInput::GameInputDeviceStatus) noexcept;
    static void CALLBACK ReadingCallback(NativeInput::GameInputCallbackToken,void*,NativeInput::IGameInputReading*) noexcept;
    template<class T> using ComPtr=Microsoft::WRL::ComPtr<T>;
    HMODULE module_=nullptr;
    ComPtr<NativeInput::IGameInput> input_;
    std::array<ComPtr<NativeInput::IGameInputDevice>,16> devices_;
    ComPtr<NativeInput::IGameInputDevice> selected_;
    ComPtr<NativeInput::IGameInputReading> previous_;
    NativeInput::GameInputCallbackToken deviceToken_=0,readingToken_=0;
    bool devicesRegistered_=false,readingsRegistered_=false,foreground_=false,baseline_=true;
    uint64_t serial_=0,generation_=1,lastReading_=0;
    std::atomic<uint64_t> callbackErrors_=0;
    InputActions::Buttons buttons_;
    mutable std::mutex mutex_;
    std::string status_="GameInput not initialized";
};
