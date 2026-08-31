#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float exposure;
uniform float saturation;
uniform float vignetteStrength;

out vec4 finalColor;

void main()
{
    vec4 source = texture(texture0, fragTexCoord) * fragColor;
    vec3 exposed = source.rgb * exposure;
    float luminance = dot(exposed, vec3(0.2126, 0.7152, 0.0722));
    vec3 graded = mix(vec3(luminance), exposed, saturation);

    vec2 centred = fragTexCoord * 2.0 - 1.0;
    float edge = smoothstep(0.30, 1.40, dot(centred, centred));
    graded *= mix(1.0, 0.68, edge * vignetteStrength);

    finalColor = vec4(clamp(graded, 0.0, 1.0), source.a);
}
