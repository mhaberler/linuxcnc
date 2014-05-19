CPPFLAGS = -g -fPIC -I.
LDFLAGS = -Wl,-rpath -Wl,.
PBCFLAGS=$(shell pkg-config --cflags  protobuf)
PBLIBS=$(shell pkg-config --libs  protobuf)

all: libjson2pb.so test_json

clean:
	-rm -f *.o *.so *.a libjson2pb.so.* test

test_json: libjson2pb.so
	$(CXX) $(PBCFLAGS) test_json.cc test.pb.cc $< -o test_json -L. $(PBLIBS) -lstdc++

test_json.o: test.pb.h

json2pb.o: bin2ascii.hh

libjson2pb.so: json2pb.o
	$(CC) $(LDFLAGS) -o $@ $^ -Wl,-soname=$@ -Wl,-h -Wl,$@ -shared -L. -lprotobuf -lstdc++ -ljansson

test.pb.h test.pb.cc: test.proto
	protoc --cpp_out=$(shell pwd) test.proto
