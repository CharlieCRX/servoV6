#ifndef AXIS_SYNC_SERVICE_H
#define AXIS_SYNC_SERVICE_H

#include "entity/Axis.h"
#include "../infrastructure/FakePLC.h"

class AxisSyncService {
public:
    // 将底层硬件的现实反馈（Feedback），同步给领域模型（Axis）
    void sync(Axis& axis, const FakePLC& plc) {
        axis.applyFeedback(plc.getFeedback());
    }
};

#endif // AXIS_SYNC_SERVICE_H