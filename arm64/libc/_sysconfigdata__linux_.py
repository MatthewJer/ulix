# The build variables of the interpreter on Ulix (chapter "Python" in lit/libc.nw, issue #133).
# CPython's own build writes a file of this name by running the interpreter it has just built; a cross-compiled interpreter cannot run on
# the machine that builds it, so this short version stands in. The module sysconfig reads it (zoneinfo, platform and site ask for a few of
# the values); what is not here is None. TZPATH is empty on purpose: there is no time zone database on the disk.
build_time_vars = {
    'ABIFLAGS': '',
    'EXT_SUFFIX': '.so',
    'MULTIARCH': '',
    'SOABI': 'cpython-312',
    'VERSION': '3.12',
    'prefix': '/usr/local',
    'exec_prefix': '/usr/local',
    'LIBDIR': '/usr/local/lib',
    'Py_ENABLE_SHARED': 0,
    'CC': 'cc',
    'TZPATH': '',
}
