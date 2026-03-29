# 自定义 GIF 表情

这个目录包含自定义的 GIF 表情文件，可以用来替换默认的 emoji 表情。

## 使用方法

要在你的开发板中使用这些 GIF 表情，需要修改 `main/CMakeLists.txt` 文件：

1. 找到你的开发板配置（例如 `CONFIG_BOARD_TYPE_ESP_BOX_3`）
2. 将 `DEFAULT_EMOJI_COLLECTION` 设置为 `custom-gif`

例如：

```cmake
elseif(CONFIG_BOARD_TYPE_ESP_BOX_3)
    set(BOARD_TYPE "esp-box-3")
    set(BUILTIN_TEXT_FONT font_noto_basic_20_4)
    set(BUILTIN_ICON_FONT font_awesome_20_4)
    set(DEFAULT_EMOJI_COLLECTION custom-gif)  # 使用自定义 GIF 表情
    set(EMOTE_RESOLUTION "320_240")
```

## 表情映射

| GIF 文件名 | 表情名称 | 别名 |
|-----------|---------|------|
| neutral.gif | neutral | - |
| idle-joker-face.gif | happy | funny |
| laughing-hearts.gif | laughing | - |
| listen1.gif | listening | - |
| listen2.gif | listening2 | - |
| shocked.gif | shocked | surprised |
| weary.gif | sad | tired, crying |
| touch-head.gif | touch_head | - |
| touch-body.gif | touch_body | - |
| touch-left-ear.gif | touch_left_ear | - |
| touch-right-ear.gif | touch_right_ear | - |
| distance-detect.gif | distance_detect | - |
| restore_factory.gif | restore_factory | - |

## 添加新的表情

1. 将你的 GIF 文件放入这个目录
2. 在 `scripts/build_default_assets.py` 中的 `custom_gif_aliases` 字典添加映射
3. 重新编译项目

## 注意事项

- GIF 文件会被打包到 assets.bin 中，然后通过 SPIFFS 文件系统加载
- 确保 GIF 文件的尺寸适合你的显示屏
- 建议使用优化的 GIF 文件以节省空间
