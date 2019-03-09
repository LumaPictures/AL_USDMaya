set(PYTHON_FILES
        maya-ufe-cmd-plugin.py
        maya-ufe-usd-plugin.py
        )

install(FILES ${PYTHON_FILES}
        DESTINATION ${CMAKE_INSTALL_PREFIX}/plugin
        )