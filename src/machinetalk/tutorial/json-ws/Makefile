# get at various _CFLAGS
include ../../../Makefile.inc

all:	demowtplugin.o
	comp --install micromot.comp


CXXFLAGS := -g -O0 -fPIC -Wall		\
	-I../protobuf 		\
	-I../../../../include \
	$(PROTOBUF_CFLAGS) 	\
	$(CZMQ_CFLAGS) 		\
	$(JANSSON_CFLAGS)	\
	$(LWS_CFLAGS)		\
	$(SSL_CFLAGS)		\
	$(UUID_CFLAGS) 		\
	$(AVAHI_CFLAGS) 	\
	$(URIPARSER_CFLAGS)


demowtplugin.o: webtalk_demoplugin.o demo.pb.o
	@echo Linking $(notdir $@)
	$(CC) -g -shared -o $@ $^
