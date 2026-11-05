# (C) 2015-2016 see Authors.txt
#
# This file is part of MPC-HC.
#
# MPC-HC is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# MPC-HC is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import sys
import os
import fnmatch
import traceback

from multiprocessing import Pool
from UpdatePOT import *
from UpdatePO import *
from UpdateRC import *


def processRC(file):
    ret = file + '\n'
    result = True
    try:
        ret += '--> Updating PO file\n'
        UpdatePO(file)
    except Exception:
        ret += traceback.format_exc()
        result = False
    else:
        try:
            ret += '--> Updating RC file\n'
            UpdateRC(file, False)
        except Exception:
            ret += traceback.format_exc()
            result = False

    ret += '----------------------'
    return result, ret

if __name__ == '__main__':
    print('Updating POT file')
    UpdatePOT()
    print('----------------------')

    pool = Pool()
    results = []
    for file in os.listdir('PO'):
        if fnmatch.fnmatch(file, 'mpc-hc.*.menus.po'):
            results.append(pool.apply_async(processRC, [file[:-9]]))

    pool.close()

    failed = False
    for result in results:
        ret = result.get(15)
        print(ret[1])
        if (not ret[0]):
            failed = True
            if not os.environ.get('CI'):
                os.system('pause')

    if failed:
        sys.exit(1)
