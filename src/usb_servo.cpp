#include <Arduino.h>
#include <errno.h>
#include <stdlib.h>
#include "STSServoDriver.h"

namespace {
bool number(char** cursor, long minimum, long maximum, long& value)
{
    char* token = strtok_r(nullptr, " ", cursor);
    if (!token) return false;
    char* end;
    errno = 0;
    value = strtol(token, &end, 10);
    return errno == 0 && end != token && *end == '\0' &&
           value >= minimum && value <= maximum;
}

bool torqueOff(STSServoDriver& servos, byte id)
{
    byte torque;
    return servos.enableTorque(id, false) &&
           servos.readRegisters(id, STSRegisters::TORQUE_SWITCH, 1, &torque) == 0 &&
           torque == 0;
}

int signedRegister(const byte* value)
{
    unsigned raw = value[0] | (value[1] << 8);
    int magnitude = raw & 0x7FFF;
    return raw & 0x8000 ? -magnitude : magnitude;
}

constexpr int lowerEndpoint = 683;
constexpr int upperEndpoint = 1365;
constexpr int endpointTolerance = 12;  // About one degree.
constexpr long maxFrequencyMilliHz = 3700;
constexpr unsigned heartbeatTimeoutUs = 1200000;

struct {
    enum Phase { idle, positioning, outbound, returning } phase = idle;
    byte id;
    uint16_t returnSpeed;
    uint32_t period;
    uint32_t cycleStart;
    uint32_t deadline;
    uint32_t nextPositionCheck;
    uint32_t heartbeat;
    unsigned cycles;
} experiment;

int readPosition(STSServoDriver& servos, byte id)
{
    byte raw[2];
    if (servos.readRegisters(id, STSRegisters::CURRENT_POSITION, sizeof(raw), raw))
        return -1;
    return signedRegister(raw);
}

bool closeTo(int position, int target)
{
    return abs(position - target) <= endpointTolerance;
}

void endExperiment(const char* reason)
{
    experiment.phase = experiment.idle;
    Serial.printf("OSC END %s %u\n", reason, experiment.cycles);
}

void advanceExperiment(STSServoDriver& servos)
{
    if (experiment.phase == experiment.idle) return;
    const uint32_t now = micros();
    if (now - experiment.heartbeat >= heartbeatTimeoutUs) {
        endExperiment("LINK");
        return;
    }

    if (experiment.phase == experiment.positioning) {
        if (int32_t(now - experiment.deadline) >= 0) {
            endExperiment("HOME");
            return;
        }
        if (int32_t(now - experiment.nextPositionCheck) < 0) return;
        experiment.nextPositionCheck = now + 20000;
    } else if (int32_t(now - experiment.deadline) < 0) {
        return;
    }

    const bool returning = experiment.phase == experiment.outbound;
    const int previousTarget = returning ? upperEndpoint : lowerEndpoint;
    const int position = readPosition(servos, experiment.id);
    if (position < 0) {
        endExperiment("SERVO");
        return;
    }
    if (!closeTo(position, previousTarget)) {
        if (experiment.phase != experiment.positioning)
            endExperiment(returning ? "LATE120" : "LATE60");
        return;
    }

    if (experiment.phase == experiment.returning) ++experiment.cycles;
    if (experiment.phase == experiment.positioning) experiment.cycleStart = now;
    if (experiment.phase == experiment.returning) experiment.cycleStart += experiment.period;

    const uint16_t speed = experiment.returnSpeed * (returning ? 1 : 2);
    const int target = returning ? lowerEndpoint : upperEndpoint;
    // Zero selects the servo's fastest acceleration profile.
    if (!servos.setMotionProfile(experiment.id, target, speed, 0)) {
        endExperiment("PROFILE");
        return;
    }
    experiment.phase = returning ? experiment.returning : experiment.outbound;
    experiment.deadline = experiment.cycleStart +
                          (returning ? experiment.period : experiment.period / 3);
}

const char* positionMove(STSServoDriver& servos, byte id, uint16_t position,
                         uint16_t speed, byte acceleration)
{
    byte mode, resolution, torque;
    if (servos.readRegisters(id, STSRegisters::OPERATION_MODE, 1, &mode) ||
        servos.readRegisters(id, STSRegisters::ANGULAR_RESOLUTION, 1, &resolution) ||
        servos.readRegisters(id, STSRegisters::TORQUE_SWITCH, 1, &torque))
        return "ERR SERVO";
    if (mode != 0 || resolution != 1) return "ERR MODE";
    if (!servos.setMotionProfile(id, position, speed, acceleration)) return "ERR PROFILE";
    if (torque != 1 &&
        (!servos.enableTorque(id, true) ||
         servos.readRegisters(id, STSRegisters::TORQUE_SWITCH, 1, &torque) ||
         torque != 1))
        return "ERR TORQUE";
    return nullptr;
}

void handleLine(STSServoDriver& servos, char* line)
{
    char* cursor;
    const char* command = strtok_r(line, " ", &cursor);
    if (!command) return;
    if (strcmp(command, "HELLO") == 0 && !strtok_r(nullptr, " ", &cursor)) {
        Serial.println("OK STS1");
        return;
    }

    long id;
    if (number(&cursor, 0, 253, id)) {
        if (strcmp(command, "SWING") == 0) {
            long frequency;
            if (!number(&cursor, 1000, 10000, frequency) ||
                strtok_r(nullptr, " ", &cursor)) {
                Serial.println("ERR COMMAND");
            } else if (experiment.phase != experiment.idle) {
                Serial.println("ERR ACTIVE");
            } else if (frequency > maxFrequencyMilliHz) {
                Serial.println("ERR RATE");
            } else {
                byte phase;
                if (servos.readRegisters(id, STSRegisters::PHASE, 1, &phase)) {
                    Serial.println("ERR SERVO");
                    return;
                }
                // STS speed units are approximately 50 or 1 encoder counts/s.
                const unsigned unit = phase & 0x04 ? 1 : 50;
                const unsigned distance = upperEndpoint - lowerEndpoint;
                // Allow 15% speed headroom for reversals, up to the 6 V no-load rating.
                const unsigned halfSpeed = (distance * 345u * frequency + 100000u * unit) /
                                           (200000u * unit);
                experiment.returnSpeed = min(halfSpeed, 3792u / unit);
                const char* error = positionMove(servos, id, lowerEndpoint, 600 / unit, 10);
                if (error) {
                    Serial.println(error);
                    return;
                }
                experiment.id = id;
                experiment.period = (1000000000u + frequency / 2) / frequency;
                experiment.cycles = 0;
                experiment.heartbeat = micros();
                experiment.deadline = experiment.heartbeat + 8000000u;
                experiment.nextPositionCheck = experiment.heartbeat;
                experiment.phase = experiment.positioning;
                Serial.println("OK SWING");
            }
            return;
        }
        if (strcmp(command, "PULSE") == 0 && !strtok_r(nullptr, " ", &cursor)) {
            if (experiment.phase == experiment.idle || experiment.id != id) {
                Serial.println("ERR INACTIVE");
            } else {
                experiment.heartbeat = micros();
                Serial.printf("OK PULSE %u %u\n", unsigned(experiment.phase),
                              experiment.cycles);
            }
            return;
        }
        if (strcmp(command, "STOP") == 0 && !strtok_r(nullptr, " ", &cursor)) {
            if (experiment.phase != experiment.idle && experiment.id == id)
                experiment.phase = experiment.idle;
            Serial.println("OK STOP");
            return;
        }
        if (strcmp(command, "STATUS") == 0 && !strtok_r(nullptr, " ", &cursor)) {
            byte mode, resolution, torque, feedback[8];
            if (servos.readRegisters(id, STSRegisters::OPERATION_MODE, 1, &mode) ||
                servos.readRegisters(id, STSRegisters::ANGULAR_RESOLUTION, 1, &resolution) ||
                servos.readRegisters(id, STSRegisters::TORQUE_SWITCH, 1, &torque) ||
                servos.readRegisters(id, STSRegisters::CURRENT_POSITION,
                                     sizeof(feedback), feedback)) {
                Serial.println("ERR SERVO");
                return;
            }
            Serial.printf("OK STATUS %d %d %u %u %u %u %u\n",
                          signedRegister(feedback), signedRegister(feedback + 2),
                          unsigned(feedback[6]), unsigned(feedback[7]),
                          unsigned(mode), unsigned(torque), unsigned(resolution));
            return;
        }
        if (strcmp(command, "RELEASE") == 0 && !strtok_r(nullptr, " ", &cursor)) {
            if (experiment.phase != experiment.idle && experiment.id == id)
                experiment.phase = experiment.idle;
            Serial.println(torqueOff(servos, id) ? "OK RELEASE" : "ERR TORQUE");
            return;
        }
        long position, speed, acceleration;
        if (strcmp(command, "MOVE") == 0 &&
            number(&cursor, 0, 4095, position) &&
            number(&cursor, 1, 32767, speed) &&
            number(&cursor, 1, 254, acceleration) &&
            !strtok_r(nullptr, " ", &cursor)) {
            const char* error = experiment.phase != experiment.idle && experiment.id == id
                ? "ERR ACTIVE" : positionMove(servos, id, position, speed, acceleration);
            Serial.println(error ? error : "OK MOVE");
            return;
        }
    }
    Serial.println("ERR COMMAND");
}
}  // namespace

void pollUsbServo(STSServoDriver& servos)
{
    advanceExperiment(servos);
    static char line[64];
    static size_t length = 0;
    static bool invalid = false;
    while (Serial.available() > 0) {
        int character = Serial.read();
        if (character == '\n') {
            if (invalid) {
                Serial.println("ERR COMMAND");
            } else {
                if (length && line[length - 1] == '\r') --length;
                line[length] = '\0';
                handleLine(servos, line);
            }
            length = 0;
            invalid = false;
            return;
        }
        if (length >= sizeof(line) - 1 || character == 0) {
            invalid = true;
        } else if (!invalid) {
            line[length++] = char(character);
        }
    }
}
