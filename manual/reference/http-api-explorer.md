# HTTP API explorer

Every endpoint of the board's HTTP API, with its parameters, request and reply schemas, and
examples, rendered from the OpenAPI description, [`openapi.json`](openapi.json). Each board serves
its own copy at `/api/v1/openapi.json`, describing the firmware it runs. Postman, Insomnia, Swagger
Editor and `openapi-generator` can import it straight from a board.

The [HTTP API](http-api.md) page explains the ideas behind it (the update session, protection, the
password) with `curl` recipes.

!!! note "Trying it out"
    **Try it out** calls a real board. Set `host` under **Servers** to its name or address, and
    use **Authorize** for its password if one is set. This site is on the board's list of
    [trusted sites](http-api.md#protection), so the board answers it.

    It works in Chrome and Edge, which may first ask to let this site reach devices on your local
    network. Safari and Firefox refuse to call a plain-http address from an https page, so use
    `curl` there, or the offline documentation served by its `serve.py`, and
    `web cors add http://localhost:8000` on the board.

    [Swagger Editor](https://editor.swagger.io) works the same way. Paste in `openapi.json`, or
    import it from the board's `/api/v1/openapi.json`.

<div id="swagger-ui"></div>
