import pytest
import requests

from miniair_ingest.opensky import TOKEN_URL, BoundingBox, OpenSkyClient, OpenSkyError, RateLimited


class FakeResponse:
    def __init__(self, status_code=200, body=None, headers=None):
        self.status_code = status_code
        self._body = body if body is not None else {}
        self.headers = headers or {}

    def json(self):
        return self._body


class FakeSession:
    """Records requests and replays queued responses, so no network is used."""

    def __init__(self, get_responses=(), post_responses=()):
        self.get_responses = list(get_responses)
        self.post_responses = list(post_responses)
        self.gets = []
        self.posts = []

    def get(self, url, params=None, headers=None, timeout=None):
        self.gets.append({"url": url, "params": params, "headers": headers or {}})
        response = self.get_responses.pop(0)
        if isinstance(response, Exception):
            raise response
        return response

    def post(self, url, data=None, timeout=None):
        self.posts.append({"url": url, "data": data})
        return self.post_responses.pop(0)


BOX = BoundingBox(50.5, -2.5, 53.0, 1.5)
STATES = FakeResponse(200, {"time": 1730000010, "states": [["4ca7b5"]]}, {"X-Rate-Limit-Remaining": "397"})
TOKEN = FakeResponse(200, {"access_token": "tok-1", "expires_in": 1800})


def test_anonymous_request_sends_box_and_no_auth_header():
    session = FakeSession(get_responses=[STATES])
    client = OpenSkyClient(session=session)

    snapshot_time, rows = client.fetch_states(BOX)

    assert snapshot_time == 1730000010.0
    assert rows == [["4ca7b5"]]
    assert session.gets[0]["params"] == {"lamin": 50.5, "lomin": -2.5, "lamax": 53.0, "lomax": 1.5}
    assert "Authorization" not in session.gets[0]["headers"]
    assert session.posts == []
    assert client.remaining_credits == 397


def test_null_states_means_no_aircraft():
    session = FakeSession(get_responses=[FakeResponse(200, {"time": 1, "states": None})])
    assert OpenSkyClient(session=session).fetch_states(BOX) == (1.0, [])


def test_authenticated_request_uses_bearer_token_and_caches_it():
    session = FakeSession(get_responses=[STATES, STATES], post_responses=[TOKEN])
    client = OpenSkyClient(session=session, client_id="id", client_secret="secret", clock=lambda: 0.0)

    client.fetch_states(BOX)
    client.fetch_states(BOX)

    assert len(session.posts) == 1  # Token fetched once, then reused
    assert session.posts[0]["url"] == TOKEN_URL
    assert session.posts[0]["data"]["grant_type"] == "client_credentials"
    assert all(g["headers"]["Authorization"] == "Bearer tok-1" for g in session.gets)


def test_token_is_refreshed_before_it_expires():
    now = [0.0]
    second_token = FakeResponse(200, {"access_token": "tok-2", "expires_in": 1800})
    session = FakeSession(get_responses=[STATES, STATES], post_responses=[TOKEN, second_token])
    client = OpenSkyClient(session=session, client_id="id", client_secret="secret", clock=lambda: now[0])

    client.fetch_states(BOX)
    now[0] = 1800.0 - 30.0  # Inside the refresh margin
    client.fetch_states(BOX)

    assert session.gets[1]["headers"]["Authorization"] == "Bearer tok-2"


def test_401_refreshes_the_token_once_and_retries():
    second_token = FakeResponse(200, {"access_token": "tok-2", "expires_in": 1800})
    session = FakeSession(get_responses=[FakeResponse(401), STATES], post_responses=[TOKEN, second_token])
    client = OpenSkyClient(session=session, client_id="id", client_secret="secret", clock=lambda: 0.0)

    assert client.fetch_states(BOX)[0] == 1730000010.0
    assert len(session.posts) == 2


def test_bad_credentials_raise_a_clear_error():
    session = FakeSession(post_responses=[FakeResponse(401)])
    client = OpenSkyClient(session=session, client_id="id", client_secret="wrong")
    with pytest.raises(OpenSkyError, match="client id/secret"):
        client.fetch_states(BOX)


def test_429_raises_rate_limited_with_retry_after():
    limited = FakeResponse(429, headers={"X-Rate-Limit-Retry-After-Seconds": "3600"})
    client = OpenSkyClient(session=FakeSession(get_responses=[limited]))
    with pytest.raises(RateLimited) as caught:
        client.fetch_states(BOX)
    assert caught.value.retry_after_s == 3600.0


def test_server_error_raises_opensky_error():
    client = OpenSkyClient(session=FakeSession(get_responses=[FakeResponse(503)]))
    with pytest.raises(OpenSkyError, match="503"):
        client.fetch_states(BOX)


def test_network_failure_raises_opensky_error():
    client = OpenSkyClient(session=FakeSession(get_responses=[requests.ConnectionError("down")]))
    with pytest.raises(OpenSkyError, match="request failed"):
        client.fetch_states(BOX)


def test_malformed_body_raises_opensky_error():
    client = OpenSkyClient(session=FakeSession(get_responses=[FakeResponse(200, {"states": []})]))
    with pytest.raises(OpenSkyError, match="unexpected"):
        client.fetch_states(BOX)


def test_bounding_box_parse_and_credit_cost():
    box = BoundingBox.parse("50.5,-2.5,53.0,1.5")
    assert box == BOX
    assert box.area_sq_deg == pytest.approx(10.0)
    assert box.credit_cost == 1
    assert BoundingBox.parse("49,-11,61,2").credit_cost == 3  # Whole UK: 156 sq deg
    assert BoundingBox.parse("-90,-180,90,180").credit_cost == 4


@pytest.mark.parametrize("text", ["1,2,3", "53,0,50,1", "50,2,53,1", "91,0,92,1", "a,b,c,d"])
def test_bounding_box_rejects_bad_input(text):
    with pytest.raises(ValueError):
        BoundingBox.parse(text)
