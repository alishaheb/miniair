"""Client for the OpenSky Network REST API (live ADS-B state vectors).

Anonymous use works but is limited to 400 API credits a day. A free
OpenSky account gives 4,000. Accounts use the OAuth2 client-credentials flow:
create an API client on your OpenSky account page and pass its id and secret
through the OPENSKY_CLIENT_ID and OPENSKY_CLIENT_SECRET environment variables.
"""

from __future__ import annotations

import logging
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

import requests

log = logging.getLogger(__name__)

API_URL = "https://opensky-network.org/api"
TOKEN_URL = "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
TOKEN_REFRESH_MARGIN_S = 60.0


class OpenSkyError(Exception):
    """The API call failed."""


class RateLimited(OpenSkyError):
    """Out of API credits. retry_after_s says how long to wait."""

    def __init__(self, retry_after_s: float) -> None:
        super().__init__(f"OpenSky rate limit reached, retry after {retry_after_s:.0f} s")
        self.retry_after_s = retry_after_s


@dataclass(frozen=True)
class BoundingBox:
    lamin: float
    lomin: float
    lamax: float
    lomax: float

    @classmethod
    def parse(cls, text: str) -> BoundingBox:
        """Parse "lamin,lomin,lamax,lomax", for example "50.5,-2.5,53.0,1.5"."""
        parts = [float(p) for p in text.split(",")]
        if len(parts) != 4:
            raise ValueError(f"bounding box needs 4 numbers, got {len(parts)}: {text!r}")
        box = cls(*parts)
        if not (-90.0 <= box.lamin < box.lamax <= 90.0 and -180.0 <= box.lomin < box.lomax <= 180.0):
            raise ValueError(f"bounding box out of range or inverted: {text!r}")
        return box

    @property
    def area_sq_deg(self) -> float:
        return (self.lamax - self.lamin) * (self.lomax - self.lomin)

    @property
    def credit_cost(self) -> int:
        """API credits one /states/all call costs for this box."""
        area = self.area_sq_deg
        if area <= 25:
            return 1
        if area <= 100:
            return 2
        if area <= 400:
            return 3
        return 4

    def as_params(self) -> dict[str, float]:
        return {"lamin": self.lamin, "lomin": self.lomin, "lamax": self.lamax, "lomax": self.lomax}


class OpenSkyClient:
    def __init__(
        self,
        session: requests.Session | None = None,
        client_id: str | None = None,
        client_secret: str | None = None,
        timeout_s: float = 15.0,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._session = session or requests.Session()
        self._client_id = client_id or None
        self._client_secret = client_secret or None
        self._timeout_s = timeout_s
        self._clock = clock
        self._token: str | None = None
        self._token_expires_at = 0.0
        self.remaining_credits: int | None = None

    @property
    def authenticated(self) -> bool:
        return self._client_id is not None and self._client_secret is not None

    def fetch_states(self, box: BoundingBox) -> tuple[float, list[list[Any]]]:
        """Return (snapshot time, state vectors) for aircraft inside the box."""
        response = self._get_states(box)
        if response.status_code == 401 and self.authenticated:
            # Token expired or was revoked: get a new one and try once more.
            self._token = None
            response = self._get_states(box)

        remaining = response.headers.get("X-Rate-Limit-Remaining")
        if remaining is not None and remaining.isdigit():
            self.remaining_credits = int(remaining)

        if response.status_code == 429:
            retry_after = response.headers.get("X-Rate-Limit-Retry-After-Seconds", "60")
            raise RateLimited(float(retry_after) if _is_number(retry_after) else 60.0)
        if response.status_code != 200:
            raise OpenSkyError(f"GET /states/all returned HTTP {response.status_code}")

        try:
            body = response.json()
            return float(body["time"]), body.get("states") or []
        except (ValueError, KeyError, TypeError) as exc:
            raise OpenSkyError(f"unexpected /states/all response: {exc}") from exc

    def _get_states(self, box: BoundingBox) -> requests.Response:
        headers = {}
        if self.authenticated:
            headers["Authorization"] = f"Bearer {self._access_token()}"
        try:
            return self._session.get(
                f"{API_URL}/states/all", params=box.as_params(), headers=headers, timeout=self._timeout_s
            )
        except requests.RequestException as exc:
            raise OpenSkyError(f"request failed: {exc}") from exc

    def _access_token(self) -> str:
        if self._token is not None and self._clock() < self._token_expires_at:
            return self._token
        try:
            response = self._session.post(
                TOKEN_URL,
                data={
                    "grant_type": "client_credentials",
                    "client_id": self._client_id,
                    "client_secret": self._client_secret,
                },
                timeout=self._timeout_s,
            )
        except requests.RequestException as exc:
            raise OpenSkyError(f"token request failed: {exc}") from exc
        if response.status_code != 200:
            raise OpenSkyError(f"token request returned HTTP {response.status_code}; check your API client id/secret")
        body = response.json()
        self._token = str(body["access_token"])
        self._token_expires_at = self._clock() + float(body.get("expires_in", 1800)) - TOKEN_REFRESH_MARGIN_S
        log.info("obtained OpenSky access token")
        return self._token


def _is_number(text: str) -> bool:
    try:
        float(text)
    except ValueError:
        return False
    return True
