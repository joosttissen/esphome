"""End-to-end cover authorization over an encrypted native API connection."""

from __future__ import annotations

import asyncio
from collections.abc import Callable

from aioesphomeapi import (
    ButtonInfo,
    CoverInfo,
    EntityState,
    SensorInfo,
    SensorState,
    api_pb2,
)
import pytest

from .types import APIClientConnectedFactory, RunCompiledFunction

NOISE_KEY = "N4Yle5YirwZhPiHHsdZLdOA73ndj/84veVaLhTvxCuU="


def _encode_varint(value: int) -> bytes:
    encoded = bytearray()
    while value > 0x7F:
        encoded.append((value & 0x7F) | 0x80)
        value >>= 7
    encoded.append(value)
    return bytes(encoded)


def _decode_varint(payload: bytes, offset: int) -> tuple[int, int]:
    value = shift = 0
    while offset < len(payload):
        byte = payload[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, offset
        shift += 7
        if shift >= 70:
            break
    raise AssertionError("malformed protobuf varint")


def _unknown_field(payload: bytes, number: int, wire_type: int, value: bytes) -> bytes:
    tag = _encode_varint((number << 3) | wire_type)
    if wire_type == 0:
        return payload + tag + value
    if wire_type == 2:
        return payload + tag + _encode_varint(len(value)) + value
    raise AssertionError(f"unsupported protobuf wire type {wire_type}")


def _varint_fields(payload: bytes) -> dict[int, int]:
    fields: dict[int, int] = {}
    offset = 0
    while offset < len(payload):
        tag, offset = _decode_varint(payload, offset)
        number, wire_type = tag >> 3, tag & 0x07
        if wire_type == 0:
            fields[number], offset = _decode_varint(payload, offset)
        elif wire_type == 1:
            offset += 8
        elif wire_type == 2:
            size, offset = _decode_varint(payload, offset)
            offset += size
        elif wire_type == 5:
            offset += 4
        else:
            raise AssertionError(f"unsupported protobuf wire type {wire_type}")
        assert offset <= len(payload), "truncated protobuf field"
    return fields


def _cover_request(
    key: int,
    *,
    position: float | None = None,
    stop: bool = False,
    toggle: bool = False,
    code: str | None = None,
) -> api_pb2.CoverCommandRequest:
    request = api_pb2.CoverCommandRequest(key=key)
    if position is not None:
        request.has_position = True
        request.position = position
    if stop:
        request.stop = True

    code_field = request.DESCRIPTOR.fields_by_name.get("code")
    toggle_field = request.DESCRIPTOR.fields_by_name.get("toggle")
    unknown_fields: list[bytes] = []
    if code is not None:
        if code_field is None:
            unknown_fields.append(_unknown_field(b"", 10, 2, code.encode("utf-8")))
        else:
            request.code = code
    if toggle:
        if toggle_field is None:
            unknown_fields.append(_unknown_field(b"", 11, 0, b"\x01"))
        else:
            request.toggle = True
    payload = request.SerializeToString() + b"".join(unknown_fields)
    return api_pb2.CoverCommandRequest.FromString(payload)


def _metadata_flag(message: api_pb2.ListEntitiesCoverResponse, name: str) -> bool:
    field = message.DESCRIPTOR.fields_by_name.get(name)
    if field is not None:
        return bool(getattr(message, name))
    # requires_code_to_open is the first free cover-info field after device_id.
    return _varint_fields(message.SerializeToString()).get(14, 0) != 0


@pytest.mark.asyncio
async def test_cover_authorization(
    yaml_config: str,
    run_compiled: RunCompiledFunction,
    api_client_connected: APIClientConnectedFactory,
) -> None:
    """Verify authorization from API requests against independent actuator counters."""
    async with (
        run_compiled(yaml_config),
        api_client_connected(noise_psk=NOISE_KEY) as client,
    ):
        raw_cover_info: dict[int, api_pb2.ListEntitiesCoverResponse] = {}

        def capture_cover_info(message: api_pb2.ListEntitiesCoverResponse) -> None:
            raw_cover_info[message.key] = message

        # If the installed aioesphomeapi schema lacks code/toggle on
        # CoverCommandRequest or requires_code_to_open on CoverInfo, preserve its
        # framing and encrypted transport while handling the wire fields directly.
        # The connection writer and raw discovery callback below are private APIs.
        client._connection.add_message_callback(
            capture_cover_info, (api_pb2.ListEntitiesCoverResponse,)
        )
        entities, _ = await client.list_entities_services()
        protected = next(
            entity
            for entity in entities
            if isinstance(entity, CoverInfo) and entity.object_id == "protected_cover"
        )
        unprotected = next(
            entity
            for entity in entities
            if isinstance(entity, CoverInfo) and entity.object_id == "unprotected_cover"
        )
        assert _metadata_flag(raw_cover_info[protected.key], "requires_code_to_open")
        assert not _metadata_flag(
            raw_cover_info[unprotected.key], "requires_code_to_open"
        )

        counters = {
            entity.object_id: entity.key
            for entity in entities
            if isinstance(entity, SensorInfo)
        }
        counter_queues: dict[int, asyncio.Queue[float]] = {}

        def on_state(state: EntityState) -> None:
            if isinstance(state, SensorState) and state.key in counter_queues:
                counter_queues[state.key].put_nowait(state.state)

        for name in (
            "protected_open_calls",
            "protected_close_calls",
            "protected_stop_calls",
            "protected_partial_calls",
            "protected_toggle_calls",
            "unprotected_open_calls",
        ):
            key = counters[name]
            counter_queues[key] = asyncio.Queue()
        client.subscribe_states(on_state)

        async def assert_action_count(
            name: str,
            expected: int,
            action: Callable[[], None],
            *,
            increment: bool,
        ) -> None:
            queue = counter_queues[counters[name]]
            while not queue.empty():
                queue.get_nowait()
            action()
            unchanged_samples = 0
            async with asyncio.timeout(3):
                while True:
                    actual = round(await queue.get())
                    assert actual in (expected, expected + 1), (
                        f"{name} counter unexpectedly became {actual}"
                    )
                    if increment and actual == expected + 1:
                        return
                    if not increment:
                        assert actual == expected, (
                            f"{name} actuator ran for a denied command"
                        )
                        unchanged_samples += 1
                        if unchanged_samples == 2:
                            return

        def send_cover(
            cover: CoverInfo,
            *,
            position: float | None = None,
            stop: bool = False,
            toggle: bool = False,
            code: str | None = None,
        ) -> None:
            request = _cover_request(
                cover.key,
                position=position,
                stop=stop,
                toggle=toggle,
                code=code,
            )
            client._get_connection().send_messages((request,))

        await assert_action_count(
            "protected_open_calls",
            0,
            lambda: send_cover(protected, position=1.0, code="2468"),
            increment=True,
        )
        await assert_action_count(
            "protected_open_calls",
            1,
            lambda: send_cover(protected, position=1.0, code="0000"),
            increment=False,
        )
        # This request has no field 10, matching an older client's wire message.
        await assert_action_count(
            "protected_open_calls",
            1,
            lambda: send_cover(protected, position=1.0),
            increment=False,
        )
        await assert_action_count(
            "protected_open_calls",
            1,
            lambda: send_cover(protected, position=1.0, code="24x8"),
            increment=False,
        )

        await assert_action_count(
            "protected_close_calls",
            0,
            lambda: send_cover(protected, position=0.0),
            increment=True,
        )
        await assert_action_count(
            "protected_stop_calls",
            0,
            lambda: send_cover(protected, stop=True),
            increment=True,
        )
        await assert_action_count(
            "protected_partial_calls",
            0,
            lambda: send_cover(protected, position=0.4),
            increment=False,
        )
        await assert_action_count(
            "protected_partial_calls",
            0,
            lambda: send_cover(protected, position=0.4, code="2468"),
            increment=True,
        )
        await assert_action_count(
            "protected_toggle_calls",
            0,
            lambda: send_cover(protected, toggle=True),
            increment=False,
        )
        await assert_action_count(
            "protected_toggle_calls",
            0,
            lambda: send_cover(protected, toggle=True, code="2468"),
            increment=True,
        )

        remote_open = next(
            entity
            for entity in entities
            if isinstance(entity, ButtonInfo) and entity.object_id == "remote_open"
        )
        await assert_action_count(
            "protected_open_calls",
            1,
            lambda: client.button_command(remote_open.key),
            increment=False,
        )
        await assert_action_count(
            "unprotected_open_calls",
            0,
            lambda: send_cover(unprotected, position=1.0),
            increment=True,
        )

        # Three submitted invalid credentials trigger the fixture's short lockout.
        for _ in range(3):
            await assert_action_count(
                "protected_open_calls",
                1,
                lambda: send_cover(protected, position=1.0, code="0000"),
                increment=False,
            )
        await assert_action_count(
            "protected_open_calls",
            1,
            lambda: send_cover(protected, position=1.0, code="2468"),
            increment=False,
        )
        await assert_action_count(
            "protected_close_calls",
            1,
            lambda: send_cover(protected, position=0.0),
            increment=True,
        )
        await assert_action_count(
            "protected_stop_calls",
            1,
            lambda: send_cover(protected, stop=True),
            increment=True,
        )

        await asyncio.sleep(1.2)
        await assert_action_count(
            "protected_open_calls",
            1,
            lambda: send_cover(protected, position=1.0, code="2468"),
            increment=True,
        )
