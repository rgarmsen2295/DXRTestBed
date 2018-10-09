#version 330 core
out vec4 color;
in vec3 vertex_color;
in vec2 fragTex;
in vec3 vertex_pos;
uniform mat4 Vi;
uniform sampler2D diffuse_tex,normal_tex;

#define PIh 1.57079632679
void main()
{
	vec3 lightposition = vec3(100,100,100);
	vec3 lightdirection = normalize(lightposition-vertex_pos);

	vec4 texcol = texture(diffuse_tex, fragTex); 
	vec4 normcol = texture(normal_tex, fragTex); 
	vec2 stc=PIh*2.0*(fragTex-vec2(0.5,0.5));

	//normcol = vec4(0,0,-1,0);

//	if(length(stc)>1.0)
	//	discard;

	vec3 normal = vec3(sin(stc.x),sin(stc.y),cos(stc.x)*cos(stc.y));
	vec3 tangent = vec3(cos(stc.x),0,sin(stc.x));
	vec3 binormal = cross(normal,tangent);

	 vec3 bumpnorm = (normcol.xyz * 2.0f) - 1.0f;

    // Calculate the normal from the data in the bump map.
    vec3 bumpNormal = (bumpnorm.x * tangent) + (bumpnorm.y * binormal) + (bumpnorm.z * normal);

	normal = (Vi* vec4(bumpNormal, 0.0)).xyz;


	float d = clamp(dot(lightdirection,normal),0,1);
	d=pow(d,1.7);

	color.b=0;
	color.rgb=normal;
	color.rgb=d*texcol.rgb;
	color.a=texcol.a;
	//color.rgb=vec3(d,d,d);
	//color.a=1;

}
