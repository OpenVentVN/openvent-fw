mkdir -p ../openvent-c
protoc  --c_out=../openvent-c *.proto


sed -i '' -e 's/protobuf-c\/protobuf-c.h/protobuf-c\/protobuf-c.h/' '../openvent-c/openvent.pb-c.h'

cp _component.mk ../openvent-c/component.mk
cp _CMakeLists.txt ../openvent-c/CMakeLists.txt
