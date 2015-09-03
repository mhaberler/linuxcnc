#ifndef PIN_HH
#define PIN_HH

#include "hal.h"
#include "hal_priv.h"
#include "hal_accessor.h"
#include <stdarg.h>

using namespace std;

class Pin
{

public:
    void *operator new(size_t size)    { throw; };
    void operator delete(void *p)      { throw; };
    void *operator new[](size_t size)  { throw; }
    void operator delete[](void *p)    { throw; };


    Pin(hal_pin_t *p) :  pin(p) {};

    Pin(const hal_pin_dir_t dir, const int owner_id, hal_s32_t defval, const char *fmt, ...) {
	hal_data_u u;
	u._s = defval;
	va_list ap;
	va_start(ap, fmt);
	pin = halg_pin_newfv(1, HAL_S32, dir, NULL, owner_id, u, fmt, ap);
	va_end(ap);
    }

    // conversions
    inline operator hal_s32_t() const { return  _get_s32_pin(pin); }


    // compound assignment +=
    inline Pin operator+=(const Pin &rhs) {
    	_incr_s32_pin(this->pin, _get_s32_pin(rhs.pin));
    	return *this;
    }
    inline Pin operator+=(const hal_s32_t& value) {
	_incr_s32_pin(this->pin, value);
	return *this;
    };
    inline Pin operator+=(const int& value) {
	_incr_s32_pin(this->pin, value);
	return *this;
    };


    inline hal_s32_t operator+(const Pin &rhs) {
    	return _get_s32_pin(this->pin) + _get_s32_pin(rhs.pin);
    }
    inline hal_s32_t operator+(const int &rhs) {
	return _get_s32_pin(this->pin) + rhs;
    }
    // Pin operator-(const Pin &);
    // Pin operator*(const Pin &);
    // Pin operator/(const Pin &);
 
    //overloaded relational operators
    // bool operator>(Pin &) const;
    // bool operator>=(Pin &) const;
    // bool operator<(Pin &) const;
    // bool operator<=(Pin &) const;
    // bool operator==(Pin &) const;
    // bool operator!=(Pin &) const;
private:
    //    hal_pin_t &pin;
    hal_pin_t *pin;
};

#endif
