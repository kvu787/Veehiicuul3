#include "GamepadInput.h"
#include <format>

GamepadInput::~GamepadInput() { Stop(); }
bool GamepadInput::Initialize(const std::filesystem::path& directory)
{
    if(input_) return true;
    module_=LoadLibraryExW((directory/L"GameInputRedist.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module_) { status_=std::format("GameInput unavailable (runtime error {}); keyboard remains available",GetLastError()); return false; }
    using InitializeFunction=HRESULT(WINAPI*)(REFIID,void**);
    const auto initialize=reinterpret_cast<InitializeFunction>(GetProcAddress(module_,"GameInputInitialize"));
    if(!initialize || FAILED(initialize(NativeInput::IID_IGameInput,reinterpret_cast<void**>(input_.GetAddressOf())))) { Stop(); status_="GameInput v3 initialization failed; keyboard remains available"; return false; }
    input_->SetFocusPolicy(NativeInput::GameInputDefaultFocusPolicy);
    status_="GameInput ready; no selected gamepad";
    auto result=input_->RegisterDeviceCallback(nullptr,NativeInput::GameInputKindGamepad,NativeInput::GameInputDeviceConnected,NativeInput::GameInputBlockingEnumeration,this,DeviceCallback,&deviceToken_);
    devicesRegistered_=SUCCEEDED(result);
    if(devicesRegistered_) { result=input_->RegisterReadingCallback(nullptr,NativeInput::GameInputKindGamepad,this,ReadingCallback,&readingToken_); readingsRegistered_=SUCCEEDED(result); }
    if(!devicesRegistered_ || !readingsRegistered_) { Stop(); status_="GameInput callback registration failed; keyboard remains available"; return false; }
    return true;
}
void GamepadInput::Stop()
{
    if(input_ && readingsRegistered_) { input_->StopCallback(readingToken_); input_->UnregisterCallback(readingToken_); }
    if(input_ && devicesRegistered_) { input_->StopCallback(deviceToken_); input_->UnregisterCallback(deviceToken_); }
    readingsRegistered_=devicesRegistered_=false;
    std::lock_guard guard(mutex_); previous_.Reset(); selected_.Reset(); for(auto& device:devices_) device.Reset(); input_.Reset();
    buttons_.Reset(); foreground_=false; baseline_=true; ++generation_; lastReading_=0;
    if(module_) { FreeLibrary(module_); module_=nullptr; }
}
void GamepadInput::SetForeground(bool value)
{
    std::lock_guard guard(mutex_);
    if(foreground_==value) return;
    foreground_=value; buttons_.Reset(); previous_.Reset(); baseline_=true; ++generation_; lastReading_=0;
}
void CALLBACK GamepadInput::DeviceCallback(NativeInput::GameInputCallbackToken,void* context,NativeInput::IGameInputDevice* device,uint64_t,NativeInput::GameInputDeviceStatus current,NativeInput::GameInputDeviceStatus) noexcept
{
    auto& self=*static_cast<GamepadInput*>(context);
    try
    {
    std::lock_guard guard(self.mutex_);
    const auto previous=self.selected_.Get();
    const bool connected=(current&NativeInput::GameInputDeviceConnected)!=0;
    if(connected)
    {
        bool known=false; for(const auto& slot:self.devices_) if(slot.Get()==device) known=true;
        if(!known) for(auto& slot:self.devices_) if(!slot) { slot=device; break; }
    }
    else for(auto& slot:self.devices_) if(slot.Get()==device) slot.Reset();
    if(self.selected_ && ((!connected && self.selected_.Get()==device) || (self.selected_->GetDeviceStatus()&NativeInput::GameInputDeviceConnected)==0)) self.selected_.Reset();
    if(!self.selected_) for(const auto& slot:self.devices_) if(slot && (slot->GetDeviceStatus()&NativeInput::GameInputDeviceConnected)!=0) { self.selected_=slot; break; }
    if(previous==self.selected_.Get()) return;
    self.previous_.Reset(); self.buttons_.Reset(); self.baseline_=true; ++self.generation_; self.lastReading_=0;
    if(self.selected_)
    {
        const NativeInput::GameInputDeviceInfo* information=nullptr;
        if(SUCCEEDED(self.selected_->GetDeviceInfo(&information)) && information)
            self.status_=std::format("GameInput: {} (VID {:04X}, PID {:04X})",information->displayName ? information->displayName : "gamepad",information->vendorId,information->productId);
    }
    else self.status_="GameInput ready; no connected gamepad (keyboard available)";
    }
    catch(...) { ++self.callbackErrors_; }
}
void CALLBACK GamepadInput::ReadingCallback(NativeInput::GameInputCallbackToken,void* context,NativeInput::IGameInputReading* reading) noexcept
{
    auto& self=*static_cast<GamepadInput*>(context);
    try
    {
    ComPtr<NativeInput::IGameInputDevice> device; reading->GetDevice(&device);
    NativeInput::GameInputGamepadState state{};
    if(!reading->GetGamepadState(&state)) return;
    std::lock_guard guard(self.mutex_);
    if(self.foreground_ && device==self.selected_) self.buttons_.Observe(static_cast<uint32_t>(state.buttons),reading->GetTimestamp());
    }
    catch(...) { ++self.callbackErrors_; }
}
GamepadSample GamepadInput::Sample()
{
    GamepadSample result;
    std::lock_guard guard(mutex_);
    result.callbackErrors=callbackErrors_.load(); result.droppedEdges=buttons_.Dropped();
    if(!input_ || !selected_ || !foreground_ || (selected_->GetDeviceStatus()&NativeInput::GameInputDeviceConnected)==0) return result;
    ComPtr<NativeInput::IGameInputReading> reading;
    NativeInput::GameInputGamepadState state{};
    if(FAILED(input_->GetCurrentReading(NativeInput::GameInputKindGamepad,selected_.Get(),&reading)) || !reading->GetGamepadState(&state)) { buttons_.Reset(); previous_.Reset(); baseline_=true; ++generation_; lastReading_=0; return result; }
    result.sampled=input_->GetCurrentTimestamp(); result.reading=reading->GetTimestamp();
    if(result.reading>result.sampled) { result.callbackErrors=++callbackErrors_; return result; }
    result.baseline=baseline_; baseline_=false;
    if(result.reading!=lastReading_) { ++serial_; lastReading_=result.reading; previous_=reading; }
    result.generation=generation_; result.callbackErrors=callbackErrors_.load();
    for(std::size_t index=0;index<devices_.size();++index) if(devices_[index]==selected_) { result.device=static_cast<uint16_t>(index); break; }
    result.serial=serial_; if(!result.baseline) { result.analog={state.rightThumbstickX,state.rightThumbstickY}; result.brake=state.leftTrigger; }
    result.edges=buttons_.Take(); result.droppedEdges=buttons_.Dropped(); result.connected=true; return result;
}
std::string GamepadInput::Status() const { std::lock_guard guard(mutex_); const auto errors=callbackErrors_.load(); return errors ? status_+std::format("; callback failures: {}",errors) : status_; }
bool GamepadInput::InputLossDetected() const { std::lock_guard guard(mutex_); return callbackErrors_.load()!=0 || buttons_.Dropped()!=0; }
