idf_component_register(SRCS "hello_world_main.c"
                    
                    REQUIRES esp_wifi
                    REQUIRES nvs_flash
                    REQUIRES esp_http_server
                    REQUIRES json
                    REQUIRES esp_timer
                    INCLUDE_DIRS "")
