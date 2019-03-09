set(arDirPath ufeScripts)

set(PYTHON_FILES
        __init__.py
        ufeParentCmd.py
        ufeSelectCmd.py
        usdRunTime.py
        usdSubject.py
        usdXformOpCmds.py
        )

install(FILES ${PYTHON_FILES}
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/python/${arDirPath}
        )