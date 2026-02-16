# Dockerfile
FROM node:18-alpine

# Установка зависимостей и lv_font_conv
RUN npm install -g lv_font_conv

WORKDIR /work

# По умолчанию — ничего не запускаем, просто готовы к работе
ENTRYPOINT ["lv_font_conv"]
