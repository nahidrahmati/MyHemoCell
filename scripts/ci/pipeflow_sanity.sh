#!/bin/bash
set -e

cd ${CI_PROJECT_DIR}/examples/pipeflow
mpirun --allow-run-as-root -n 4 ./pipeflow ../../scripts/ci/config-pipeflow.xml
cd tmp/log
if [ -n "`cat logfile | grep "# of cells" | cut -d: -f2 | cut -d" " -f2 | grep -v 42`" ]; then
  echo "Error in number of cells, 42 Expected"
  exit 1
fi
if [ -n "`cat logfile |grep "viscosity" | cut -d: -f 4 |cut -d" " -f2 | xargs -n 1 echo 1.03 \< | bc | grep -v 1`" ]; then
  echo "Error, viscosity goes below 1.03"
  exit 1
fi
if [ -n "`cat logfile |grep "viscosity" | cut -d: -f 4 |cut -d" " -f2 | xargs -n 1 echo 3.0 \> | bc | grep -v 1`" ]; then
  echo "Error, viscosity goes above 3.0"
  exit 1
fi
if [ -n "`cat logfile |grep "Force  -" | cut -d: -f 3 |cut -d" " -f2 | xargs -n 1 echo 4.0 \> | sed -e 's/[eE]+*/\*10\^/' | bc | grep -v 1`" ]; then
  echo "Error, Max force goes above 4.0"
  exit 1
fi

echo "Checking for similar output, differing CPU's"

cd ../../
mpirun --allow-run-as-root -n 2 ./pipeflow ../../scripts/ci/config-pipeflow.xml

if [ -n "`diff -C 0 tmp/log/logfile tmp_0/log/logfile  | tail -n +2 | grep -v atomic-block |grep -v Voxelizer | grep -v '\-\-\-' | grep -v '\*\*\*'`" ]; then
  echo "Output of differing CPU's is not identical, indicating some boundary problem probably"
  exit 1
fi

echo "Checking checkpointing"
cd tmp/checkpoint/
if [ ! -s "lattice.dat" ] || [ ! -s "lattice.dat.old" ]; then
  echo "Error in checkpointing lattice output"
  exit 1
fi
if [ ! -s "particleField.dat" ] || [ ! -s "particleField.dat.old" ]; then
  echo "Error in checkpointing particleField output"
  exit 1
fi
if [ ! -s "checkpoint.xml" ] || [ ! -s "checkpoint.xml.old" ]; then
  echo "Error in checkpointing xml output"
  exit 1
fi
