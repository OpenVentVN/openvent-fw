# OPEN SOURCE VENTILATOR

![Made with love in Vietnam](https://madewithlove.now.sh/vn?heart=true)

## Our Idea

We created this project with the purpose of helping the community to manufacture their own simple Ventilator with low production cost. All information about the project will be publicly displayed. From mechanical drawings, circuit system drawings, electricity, source code is publicly available and free to download.

## Our motivation

Vietnam and the world are in the most intense battle again COVID-19. This Ventilator project will alleviate the ongoing shortage of respiratory devices in most health facilities, not only in Vietnam but also in the world. This can contribute a small part in offering the best treatment to patients who are infected with COVID-19.

A Ventilator is a medical device that plays an important role in emergency and health assistance. Therefore, we also hope that not only in the COVID-19 but also in life, our project can bring many practical contributions to the community and society.

## Technical specification


## Getting started


### Setting Up ESP-IDF

[See setup guides for detailed instructions to set up the ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/)

### Quick Start

```bash
git clone --recursive https://github.com/OpenVentVN/openvent-fw.git
cd openvent-fw
make menuconfig
export ESPPORT=/dev/ttyUSB0
export ESPBAUD=1497600
make flash monitor
```

## License

[Apache License 2.0](./LICENSE)
