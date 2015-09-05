
#include <LCNC_Headers.h>

using namespace std;

struct __comp_state 
{
    float_pin_ptr out;
    float_pin_ptr value;
};


class Const
{
public:
    Const(int argc, char** argv);
    ~Const();

private:    
    int connectPins();
    void eventsLoop();

    int comp_id;
    char prefix[12];
    struct __comp_state *inst;
};


Const::Const(int argc, char** argv)
{
    int r;
    
    strcpy(prefix, "constv2\0");  // used later in pins
    comp_id = hal_init("constv2");
    
    if(comp_id < 0) 
        exit(-1);
        
    if((r = connectPins())) 
        hal_exit(comp_id);    
    else
        {
	    hal_ready(comp_id);
	    cout << "Component \""<< prefix << "\" registered and ready\n" << "comp_ID = " << comp_id << "\n" << endl;
        }

    eventsLoop();
}


Const::~Const()
{
    hal_exit(comp_id);
}


int Const::connectPins()
{
    int r = 0;
    int sz = sizeof(struct __comp_state);

    inst = (struct __comp_state*)hal_malloc(sz);
    memset(inst, 0, sz);
    
    inst->out = halx_pin_float_newf(HAL_OUT, comp_id,"%s.out", prefix);
    if (float_pin_null(inst->out))
	return _halerrno;
    set_float_pin(inst->out, 0.0);
    
    inst->value = halx_pin_float_newf(HAL_IN, comp_id,"%s.value", prefix);
    if (float_pin_null(inst->value))
	return _halerrno;
    set_float_pin(inst->value, 0.0);

    return 0;
}


void Const::eventsLoop()
{
    hal_float_t tmp = 0, old_tmp = 0;

    while(1)
        {
	    tmp = get_float_pin(inst->value);    
	    if(tmp != old_tmp)
		{
		    cout << "\nvalue from out pin = " << tmp << endl;
		    old_tmp = set_float_pin(inst->out, tmp);    
		    cout << "\nvalue from set_float_pin = " << old_tmp << endl;
		    old_tmp = tmp;
		}
        
	    usleep(500000);
        }
}

extern "C" int main(int argc, char *argv[])
{
    Const c(argc, argv);
    
    return 0;
}
