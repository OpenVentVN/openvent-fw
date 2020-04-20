idf_component_register(SRCS "app_config.c"
                            "proto/c/app_config.pb-c.c"
                    INCLUDE_DIRS include
                    PRIV_INCLUDE_DIRS proto/c
                    PRIV_REQUIRES protobuf-c)
