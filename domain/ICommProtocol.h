// F:/project/servoV6/domain/ICommProtocol.h
#ifndef I_COMM_PROTOCOL_H
#define I_COMM_PROTOCOL_H
#include <string>
#include <vector>
#include <cstdint>
struct RegisterBlock {
    std::vector<uint16_t> data;
};
class ICommProtocol {
public:
    virtual ~ICommProtocol() {}

    virtual bool open(const std::string& deviceName, bool reOpen = false) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    virtual bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) = 0;
    virtual bool write(int mID, int regType, int reg, const RegisterBlock& in) = 0;
};


#endif // I_COMM_PROTOCOL_H
