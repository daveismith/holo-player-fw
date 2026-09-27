# HTTP API explorer

Every endpoint of the board's HTTP API, with its parameters, request and reply schemas, and
examples, rendered from the OpenAPI description, [`openapi.json`](openapi.json). Each board serves
its own copy at `/api/v1/openapi.json`, describing the firmware it runs. Postman, Insomnia, Swagger
Editor and `openapi-generator` can import it straight from a board.

The [HTTP API](http-api.md) page explains the ideas behind it (the update session, protection, the
password) with `curl` recipes.

!!! note "Trying it out"
    This page describes the API, but can't call a board. A page on this site can't reach a board
    on your network: over https the browser blocks plain-http requests, and the board doesn't
    answer other sites' pages anyway (that is part of its [protection](http-api.md#protection)).
    Use `curl`, a script, or the board's own web app.

<div id="swagger-ui"></div>
