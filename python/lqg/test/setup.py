#/usr/bin/evn python

from distutils.core import setup, Extension, Command
import numpy

module1 = Extension('rtkf',
	sources = ['rtkf_module.c', 'pios_heap.c', 
	           '../../../flight/Libraries/lqg_rate/rate_torque_kf.c', 
	           '../../../flight/Libraries/lqg_rate/rate_torque_kf_optimize.cpp', 
	           '../../../flight/Libraries/lqg_rate/dare.cpp'],
	include_dirs=['../../../flight/Libraries/lqg_rate','../../../flight/PiOS/inc',numpy.get_include()])

module2 = Extension('rtsi',
	sources = ['rtsi_module.c', 'pios_heap.c', '../../../flight/Libraries/lqg_rate/rate_torque_si.c'],
	include_dirs=['../../../flight/Libraries/lqg_rate','../../../flight/PiOS/inc',numpy.get_include()],
    extra_compile_args=['-std=gnu99'],)

module3 = Extension('rtlqr',
	sources = ['rtlqr_module.c',
	           '../../../flight/Libraries/lqg_rate/rate_torque_lqr_optimize.cpp',
	           '../../../flight/Libraries/lqg_rate/dare.cpp'],
	include_dirs=['../../../flight/Libraries/lqg_rate', '../../../flight/PiOS/inc', numpy.get_include()],)

module4 = Extension('dare',
	sources = ['dare_module.cpp', '../../../flight/Libraries/lqg_rate/dare.cpp'],
	include_dirs=['../../../flight/Libraries/lqg_rate', '../../../flight/PiOS/inc', numpy.get_include()],)

module5 = Extension('qc_ins',
	sources = ['qc_ins_module.c', 'pios_heap.c',  '../../../flight/Libraries/lqg_nav/qc_ins.c'],
	include_dirs=['../../../shared/api/', '../../../flight/Libraries/lqg_nav', '../../../flight/PiOS/inc', numpy.get_include()],)
 
setup (name = 'RateTorqueLQG',
        version = '1.0',
        description = 'Rate Torque LQG python test code',
        ext_modules = [module1, module2, module3, module4, module5])
