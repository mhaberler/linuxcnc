#ifndef PIN_HH
#define PIN_HH

#include "hal.h"
#include "hal_priv.h"
#include "hal_accessor.h"

using namespace std;

class Pin
{

public:
    void *operator new(size_t size);
    void operator delete(void *p);
    void *operator new[](size_t size);
    void operator delete[](void *p);

    //constructor
    // Pin(hal_pin_t &p) : pin(p) {};
    Pin(hal_pin_t *p) :  pin(p) {};

    // value retrieval
    // hal_s32_t operator=(const Pin& that);
    //    Pin operator=(const hal_s32_t& value);

    operator hal_s32_t() const { return  _get_s32_pin(pin); }

    
    //overloaded operations +, -, * and /
    Pin operator+(const Pin &);
      Pin operator+(const int &);
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
