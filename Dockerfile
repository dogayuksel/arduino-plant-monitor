FROM node:14.15.1-alpine3.12

WORKDIR /usr/app/

COPY package.json package.json

COPY dist dist

RUN yarn && yarn cache clean

RUN addgroup -S appgroup && \
    adduser -S appuser appgroup && \
    chown -R appuser:appgroup /usr/app

USER appuser

EXPOSE 3000

CMD ["yarn", "start"]
