idf_build_get_property(target IDF_TARGET)

message(STATUS "→ Building example for ${target}")
# Uncomment only one app_source as an entry point for your Firmware

# 1. Recommended to start with the simplest demo. No WiFi or extra configuration needed: 
set(app_sources "switch/getting-started/on-off-switch.cpp")

set(app_requires
        # I2C peripherals: Touch FT6X36 and RTC
        #                  DS3231 that will be on board C3 controller rel 1.1      
        "i2c"
        # For SPI epapers
        "epaper-spi"
        
        # Other IDF common components:
        "nvs_flash"
        "esp-tls"
        "esp_http_client"
        "protocol_examples_common"
        
        # Common fonts
        "Fonts"
        
        # This leaving for later on (not in 1.0)
        # jpeg decoder
        #"jpegdec"
        # Only for asian unicode fonts
        #"OpenFontRender"
        # Espressif IDF own components:
        #"bt"  # esp_bt (bluetooth)
)

#INCLUDE_DIRS "switch/translations"
idf_component_register(SRCS ${app_sources}
        REQUIRES ${app_requires}
        #INCLUDE_DIRS "Fonts"
        )
