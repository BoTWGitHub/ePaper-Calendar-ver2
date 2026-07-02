idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES epd_7in5_v2 spi wifi time_manager config json_reader storage spiffs http_client ics_parser epd_lvgl_bridge calendar_ui)
