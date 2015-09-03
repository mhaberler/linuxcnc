#include <iostream>
#include "pin.hh"
#include "hal_priv.h"

#include <stdlib.h>
#include <stdexcept>
using namespace std;

void *Pin::operator new(size_t size)
{
    void *p;
    cout << "In overloaded new.\n";
    p =  malloc(size);
    if(!p) 
    {
	throw   invalid_argument("malloc failed in new");
    }
    return p;
}

void Pin::operator delete(void *p)
{
    cout << "In overloaded delete.\n";
    free(p);
}

void *Pin::operator new[](size_t size)
{
    void *p;
    cout << "Using overload new[].\n";
    p =  malloc(size);
    if(!p) 
    {
        throw   invalid_argument("malloc failed in new[]");
    }
    return p;
}

void Pin::operator delete[](void *p)
{
    cout << "Free array using overloaded delete[]\n";
    free(p);
}


// Pin::Pin(hal_pin_t *p)
// {

// }

//overloaded + operator allows addition of rational numbers
Pin Pin::operator+(const Pin &right)
{
  Pin temp = *this;
    cout << "plusop \n";
  return temp;
}

// hal_s32_t Pin::operator=(const Pin& that)
// {
//       cout << "getop \n";

//     return _get_s32_pin(that.pin);
// }


// Pin Pin::operator=(const hal_s32_t& value)
// {
//     return _set_s32_pin(this->pin, value);
// }


Pin Pin::operator+(const int &right)
{
    //  Pin temp = 
  cout << "plusop \n";
  _incr_s32_pin(this->pin, right);

  return *this; // _get_s32_pin(this->pin);
}

#if 0
//overloaded - operator allows subtraction of rational numbers
Pin Pin::operator-(const Pin &right)
{
  Pin temp = *this;
  
  //cross multiply and subtract denominators
  int num = (temp.numerator * right.denominator) - (right.numerator * temp.denominator);
  
  //multiply numerators
  int den = (temp.denominator * right.denominator);
  
  //find gcd and simplify
  int divisor = gcd(num,den);

  //can't have a negative divisor
  if(divisor<0)
    divisor=divisor*-1;

  if(divisor==0)
    throw invalid_argument("GCD can not be 0!");
  else
    {
      temp.numerator = num/divisor;
      temp.denominator = den/divisor;
    }
  
  return temp;
}

//overloaded * operator allows multiplication of rational numbers
Pin Pin::operator*(const Pin &right)
{
  Pin temp = *this;

  //multiply numerators
  int num = temp.numerator * right.numerator;

  //multiply denomoninators
  int den = (temp.denominator * right.denominator);

  //find gcd and simplify
  int divisor = gcd(num,den);

  if(divisor==0)
    throw invalid_argument("GCD can not be 0!");
  else
    {
      temp.numerator = num/divisor;
      temp.denominator = den/divisor;
    }

  return temp;
}

//overloaded / operator allows division of rational numbers
Pin Pin::operator/(const Pin &right)
{
  Pin temp = *this;

  //cross multiply by reciprocal of right
  int num = temp.numerator * right.denominator;

  //cross multiply by reciprocal of right
  int den = (temp.denominator * right.numerator);

  //find gcd and simplify
  int divisor = gcd(num,den);

  if(divisor==0)
    throw invalid_argument("GCD can not be 0!");
  else
    {
      temp.numerator = num/divisor;
      temp.denominator = den/divisor;
    }

  return temp;
}

//overloaded > operator
bool Pin::operator>(Pin &right) const
{
  Pin temp = *this;
  Pin difference = temp-right;
  if(difference.numerator > 0)
    return true;
  else
    return false;
}

//overloaded >= operator
bool Pin::operator>=(Pin &right) const
{
  Pin temp = *this;
  Pin difference = temp-right;
  if(difference.numerator >= 0)
    return true;
  else
    return false;
}

//overloaded < operator
bool Pin::operator<(Pin &right) const
{
  Pin temp = *this;
  Pin difference = temp-right;
  if(difference.numerator < 0)
    return true;
  else
    return false;
}

//overloaded <= operator
bool Pin::operator<=(Pin &right) const
{
  Pin temp = *this;
  Pin difference = temp-right;
  if(difference.numerator <= 0)
    return true;
  else
    return false;
}

//overloaded == operator
bool Pin::operator==(Pin &right) const
{
  Pin temp = *this;
  Pin difference = temp-right;
  if(difference.numerator == 0)
    return true;
  else
    return false;
}

//overloaded != operator
bool Pin::operator!=(Pin &right) const
{
  Pin temp = *this;
  Pin difference = temp-right;
  if(difference.numerator != 0)
    return true;
  else
    return false;
}
#endif
