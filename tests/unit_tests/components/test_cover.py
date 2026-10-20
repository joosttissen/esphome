"""Tests for generic cover authorization configuration."""

import pytest

from esphome.components.cover import (
    _COVER_AUTHORIZATION_SCHEMA,
    _validate_authorization_pin,
    _validate_authorization_transport,
    _validate_lockout,
)
import esphome.config_validation as cv
from esphome.final_validate import full_config


@pytest.mark.parametrize("pin", ["1234", "012345678901"])
def test_authorization_pin_valid(pin: str) -> None:
    assert _validate_authorization_pin(pin) == pin


@pytest.mark.parametrize("pin", ["123", "1234567890123", "12a4", "１２３４"])
def test_authorization_pin_invalid(pin: str) -> None:
    with pytest.raises(cv.Invalid):
        _validate_authorization_pin(pin)


def test_authorization_lockout_rejects_zero_and_overflow() -> None:
    for value in ("0ms", "50000d"):
        with pytest.raises(cv.Invalid):
            _validate_lockout(value)


def test_authorization_defaults() -> None:
    config = _COVER_AUTHORIZATION_SCHEMA({"opening": {"pin": "1234"}})
    assert config["opening"]["max_attempts"] == 5
    assert config["opening"]["lockout"].total_milliseconds == 300000


def test_authorization_requires_encrypted_api() -> None:
    config = [{"authorization": {"opening": {"pin": "1234"}}}]
    token = full_config.set({"api": {}})
    try:
        with pytest.raises(cv.Invalid, match="api.encryption.key"):
            _validate_authorization_transport(config)
    finally:
        full_config.reset(token)


def test_authorization_accepts_encrypted_api() -> None:
    config = [{"authorization": {"opening": {"pin": "1234"}}}]
    token = full_config.set({"api": {"encryption": {"key": "test"}}})
    try:
        assert _validate_authorization_transport(config) is config
    finally:
        full_config.reset(token)
