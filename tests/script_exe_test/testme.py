import sys
sys.path.insert(0, '..')
from cde_test_common import *

def checker_func():
  assert os.path.isfile('cde-root/bin/sh')

generic_test_runner(["./script_exe_test.sh"], checker_func)
