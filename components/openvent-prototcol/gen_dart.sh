mkdir -p ../openvent-dart
protoc --dart_out=../openvent-dart/ -I . *.proto
