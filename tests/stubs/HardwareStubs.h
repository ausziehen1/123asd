#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <vector>
#include <map>
#include <string>
#define CONFIG_IDF_TARGET_ESP32S3 1
#define ARDUINO_USB_MODE 0
#define DRAM_ATTR
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 1
#define ESP_OK 0
#define ESP_TIMER_TASK 0
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
#define configMAX_PRIORITIES 25
using BaseType_t=int;
using TaskHandle_t=void*;
using esp_timer_handle_t=void*;
using esp_err_t=int;
using gpio_num_t=int;
struct esp_timer_create_args_t {
    void (*callback)(void*); void* arg; int dispatch_method; const char* name;
};
namespace fake {
struct Stop {};
struct Report { uint64_t at; int16_t x,y; };
static uint64_t now=0, end=0, wake=0;
static uint32_t rng=7;
static bool isMounted=true, isSuspended=false, isReady=true;
static bool prefsWriteOk=true, rejectReports=false;
static std::vector<Report> reports;
static std::function<bool(uint64_t)> trigger=[](uint64_t){return false;};
static std::function<void(uint64_t)> step=[](uint64_t){};
static std::function<void()> onDelay=[](){};
inline void check() { step(now); if(now>=end) throw Stop(); }
}
inline int64_t esp_timer_get_time(){ fake::check(); return fake::now++; }
inline unsigned long millis(){fake::check(); return uint32_t(fake::now/1000);}
inline int gpio_get_level(gpio_num_t pin){return pin==5 && fake::trigger(fake::now)?0:1;}
inline int digitalRead(int pin){return gpio_get_level(pin);}
inline void pinMode(int,int){}
inline void neopixelWrite(int,uint8_t,uint8_t,uint8_t){}
template<class T> inline T constrain(T x,T a,T b){return std::min(b,std::max(a,x));}
inline long map(long x,long a,long b,long c,long d){return (x-a)*(d-c)/(b-a)+c;}
inline void esp_rom_delay_us(uint32_t us){fake::now+=us;}
inline esp_err_t esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t* h){*h=(void*)1;return ESP_OK;}
inline esp_err_t esp_timer_start_once(esp_timer_handle_t,uint64_t us){fake::wake=fake::now+us;return ESP_OK;}
inline uint32_t ulTaskNotifyTake(int,uint32_t timeout){if(timeout)fake::now=std::max(fake::now,fake::wake);return 1;}
inline void xTaskNotifyGive(TaskHandle_t){}
inline void vTaskDelay(uint32_t ticks){fake::onDelay();fake::now+=ticks*1000;fake::check();}
inline void delay(unsigned long ms){vTaskDelay(ms);}
inline void vTaskSuspend(TaskHandle_t){throw fake::Stop();}
inline void vTaskDelete(TaskHandle_t){}
inline BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,int,TaskHandle_t* h,int){*h=(void*)1;return pdPASS;}
inline int esp_task_wdt_add(TaskHandle_t){return ESP_OK;}
inline int esp_task_wdt_reset(){return ESP_OK;}
inline uint32_t esp_random(){fake::rng^=fake::rng<<13;fake::rng^=fake::rng>>17;fake::rng^=fake::rng<<5;return fake::rng;}
inline bool tud_mounted(){return fake::isMounted;}
inline bool tud_suspended(){return fake::isSuspended;}
inline bool tud_hid_n_ready(int){return fake::isMounted && !fake::isSuspended && fake::isReady;}
inline bool tud_hid_n_report(int,uint8_t,const void* p,size_t n){
 if(!tud_hid_n_ready(0) || fake::rejectReports)return false;
 int16_t x,y; std::memcpy(&x,(const char*)p+1,2);std::memcpy(&y,(const char*)p+3,2);
 if(n!=6)throw fake::Stop();
 fake::reports.push_back({fake::now,x,y});return true;
}
class USBHIDDevice {public: virtual uint16_t _onGetDescriptor(uint8_t*)=0; virtual ~USBHIDDevice()=default;};
class USBHID {public:void addDevice(USBHIDDevice*,size_t){} void begin(){}};
struct FakeUSB {
 void VID(int){} void PID(int){} void usbVersion(int){} void usbPower(int){}
 void usbAttributes(int){} void productName(const char*){}void manufacturerName(const char*){}
 void serialNumber(const char*){}bool begin(){return true;}
};
static FakeUSB USB;
class Preferences {
public:
 std::map<std::string,int> data;
 bool begin(const char*,bool){return true;}
 int getInt(const char* k,int fallback){auto i=data.find(k);return i==data.end()?fallback:i->second;}
 size_t putInt(const char* k,int value){if(!fake::prefsWriteOk)return 0;data[k]=value;return 4;}
};
