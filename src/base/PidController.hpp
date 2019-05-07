#ifndef __PID_CONTROLLER_H__
#define __PID_CONTROLLER_H__

#include "Headers.hpp"

class PidController {
 public:
  // Kp -  proportional gain
  // Ki -  Integral gain
  // Kd -  derivative gain
  // dt -  loop interval time
  // max - maximum value of manipulated variable
  // min - minimum value of manipulated variable
  PidController(double dt, double max, double min, double Kp, double Kd,
                double Ki);

  // Returns the manipulated variable given a setpoint and current process value
  double calculate(double setpoint, double pv);
  ~PidController();

 private:
  double _dt;
  double _max;
  double _min;
  double _Kp;
  double _Kd;
  double _Ki;
  double _pre_error;
  double _integral;
};

#endif