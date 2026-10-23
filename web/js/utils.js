function update_text(element, value) {
    const text = String(value);

    if (element.textContent !== text) {
        element.textContent = text;
    }
}

function normalize_angle(angle) {
    return ((angle + 180) % 360 + 360) % 360 - 180;
}

function lerp_angle(current, target, factor) {
    const diff = normalize_angle(target - current);
    return current + diff * factor;
}