# Miscellaneous Notes

## Some esptool Incantations

    esptool -p <port> chip-id

    esptool -p <port read-flash 0x9000 20480 ~/tmp/nvs

    hd ~/tmp/nvs|less

    esptool -p <port erase-region 0x9000 20480
