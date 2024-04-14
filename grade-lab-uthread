#!/usr/bin/env python3

import re
from gradelib import *

r = Runner(save("xv6.out"))

@test(100, "uthread")
def test_uthread():
    r.run_qemu(shell_script([
        'uthread'
    ]))
    expected = ['thread_a started', 'thread_b started', 'thread_c started']
    expected.extend(['thread_%s %d' % (tid, n) for n in range(100) for tid in ('c', 'a', 'b')])
    expected.extend(['thread_c: exit after 100', 'thread_a: exit after 100', 'thread_b: exit after 100'])
    expected.append('thread_schedule: no runnable threads')
    if not re.findall('\n'.join(expected), r.qemu.output, re.M):
        raise AssertionError('Output does not match expected output')

run_tests()
