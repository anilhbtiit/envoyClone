from aiohttp import web

routes = web.RouteTableDef()


@routes.get("/service")
async def get(request):
    return web.Response(text=f"Hello {request.headers.get('x-current-user')} from behind Envoy!")


if __name__ == "__main__":
    app = web.Application()
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
