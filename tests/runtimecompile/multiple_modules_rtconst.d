
// RUN: %ldc -enable-runtime-compile -I%S %s %S/inputs/rtconst_owner.d %S/inputs/rtconst_user.d  -run
// RUN: %ldc -enable-runtime-compile -singleobj -I%S %s %S/inputs/rtconst_owner.d %S/inputs/rtconst_user.d -run

import ldc.runtimecompile;

import inputs.rtconst_owner;
import inputs.rtconst_user;

void main(string[] args)
{
  compileDynamicCode();
  assert(11 == getValue());
  value = 2;
  assert(11 == getValue());
  compileDynamicCode();
  assert(12 == getValue());
}
