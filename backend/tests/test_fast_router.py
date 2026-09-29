from app.fast_router import FastRouter


def test_routes_greetings_and_short_acknowledgements_to_gemma():
    router = FastRouter()

    assert router.route("Hola Buddy").target == "gemma"
    assert router.route("Gracias!").target == "gemma"
    assert router.route("¿Cómo estás?").target == "gemma"


def test_routes_reminders_and_information_requests_to_grokbot():
    router = FastRouter()

    assert router.route("Recuérdame llamar a María mañana").target == "grokbot"
    assert router.route("¿Qué tiempo hace en Caracas?").target == "grokbot"
    assert router.route("Busca información sobre Gemma").target == "grokbot"
