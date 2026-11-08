# SPIFFS Assets Builder

`build.py` packs the wake-word model, text font and emoji images into the `assets.bin`
partition image flashed to the device.

```bash
./build.py --wakenet_model <wakenet_model_dir> \
    --text_font <text_font_file> \
    --emoji_collection <emoji_collection_dir>
```

`build_all.py` builds every combination of the models, fonts and emoji sets listed in `main()`.
