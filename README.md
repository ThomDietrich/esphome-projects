# My ESPHome Projects

Welcome to my collection of private [ESPHome](https://esphome.io/index.html) projects.

This project uses [pipenv](https://pipenv.pypa.io/en/latest/) to ensure proper dependency management.

Usage in short:

```sh
pipenv install
pipenv shell

# Configure your wifi credentials via 'secrets.yaml'

esphome run food-dispenser.yaml
esphome -s device_name food_dispenser_1 -s device_friendly_name "Food Dispenser" -s device_area "Barn" run food-dispenser.yaml
```

Good luck and have fun!
Feel free to contact me with questions.
