#pragma once
#include "application/axis/IAxisDriver.h"
class EnableUseCase {
public:
    explicit EnableUseCase(IAxisDriver& driver) : driver_(driver) {}

    RejectionReason execute(Axis& axis, bool active) {
        if (!axis.enable(active)) {
            return axis.lastRejection();
        }

        if (axis.hasPendingCommand()) {
            driver_.send(axis.getPendingCommand());
        }
        return RejectionReason::None;
    }

private:
    IAxisDriver& driver_;
};