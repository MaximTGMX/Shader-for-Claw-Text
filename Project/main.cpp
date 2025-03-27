#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <iostream>
#include <map>

// Vertex Shader
const char* vertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec4 vertex;
out vec2 TexCoords;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vertex.zw;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D text;
uniform float edgeIntensity;
uniform float interiorIntensity;
uniform vec2 highlightCenter;
uniform float highlightRadius;
uniform bool useHighlight;
uniform bool useBevel;
uniform float bevelStrength;
uniform bool useGrayscale;
uniform float medianRadius = 1.5;
uniform float percentile = 0.5;
uniform float alphaPercentile = 0.5;
uniform bool highPrecision = false;
uniform int clippingMode = 0;       // 0=adjust, 1=clip, 2=background
uniform vec4 clipColor = vec4(0.0);
uniform vec2 pixelSize = vec2(1.5);
uniform bool pixelize = true;
uniform bool isShadow = false;

// Color palettes for different intensity ranges
const vec3 shadowColors[7] = vec3[]( 
    vec3(0.43, 0.32, 0.14), vec3(0.33, 0.22, 0.07), vec3(0.59, 0.33, 0.07), 
    vec3(0.71, 0.46, 0.13), vec3(0.6, 0.48, 0.25), vec3(0.51, 0.4, 0.31), vec3(0.72, 0.54, 0.28)
);

const vec3 midtoneColors[8] = vec3[]( 
    vec3(0.8, 0.71, 0.3), vec3(0.67, 0.59, 0.42), vec3(0.83, 0.78, 0.56), 
    vec3(0.95, 0.76, 0.3), vec3(0.96, 0.84, 0.36), vec3(0.99, 0.9, 0.35), vec3(0.97, 0.95, 0.51), vec3(0.99, 0.93, 0.52)
);

const vec3 highlightColors[7] = vec3[]( 
    vec3(0.99, 0.93, 0.42), vec3(0.99, 0.99, 0.68), vec3(0.94, 0.94, 0.94), 
    vec3(1.0, 1.0, 1.0), vec3(0.91, 0.9, 0.72), vec3(0.99, 0.96, 0.83), vec3(1.0, 0.94, 0.94)
);

// Clamp UV coordinates to avoid texture edge artifacts
vec2 clampUV(vec2 uv) {
    return clamp(uv, vec2(0.001), vec2(0.999));
}

// Safe alpha sampling with minimum value to avoid division by zero
float safeAlpha(vec2 uv) {
    return max(texture(text, clampUV(uv)).r, 0.01);
}

// Random and noise functions for texture patterns
float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = rand(i);
    float b = rand(i + vec2(1.0, 0.0));
    float c = rand(i + vec2(0.0, 1.0));
    float d = rand(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// Data structure for median blur calculations
struct PixelData {
    vec3 color;
    float alpha;
    float luminance;
};

// Simple bubble sort for median calculation
void sortPixels(inout PixelData[9] pixels, int count) {
    for (int i = 0; i < count-1; i++) {
        for (int j = 0; j < count-i-1; j++) {
            if (pixels[j].luminance > pixels[j+1].luminance) {
                PixelData temp = pixels[j];
                pixels[j] = pixels[j+1];
                pixels[j+1] = temp;
            }
        }
    }
}

// Main color calculation function - determines final pixel color based on various effects
vec3 calculateFinalColor(vec2 uv) {
    if (isShadow) return vec3(0.0);
    float alpha = texture(text, uv).r;
    if (alpha < 0.1) return vec3(0.0);

    // Edge detection using Sobel-like gradient
    vec2 texelSize = 1.0 / textureSize(text, 0) * 2.0;
    float left = texture(text, uv - vec2(texelSize.x, 0.0)).r;
    float right = texture(text, uv + vec2(texelSize.x, 0.0)).r;
    float top = texture(text, uv + vec2(0.0, texelSize.y)).r;
    float bottom = texture(text, uv - vec2(0.0, texelSize.y)).r;

    float gradientX = left - right;
    float gradientY = bottom - top;
    float edge = sqrt(gradientX * gradientX + gradientY * gradientY);
    edge = smoothstep(0.0, 0.01, edge);

    // Texture pattern generation for interior regions
    vec2 texScale = vec2(20.0);
    float texturePattern = noise(uv * texScale);
    texturePattern = texturePattern * 4.0 - 0.2;
    float textureStrength = smoothstep(0.1, 0.3, 1.0 - edge);
    float texturedIntensity = interiorIntensity + texturePattern * textureStrength;

    // Base intensity calculation mixing edge and interior
    float baseIntensity = mix(texturedIntensity, edgeIntensity, edge);
    baseIntensity = mix(baseIntensity, texture(text, uv).r, 0.6);

    // Bevel effect - simulates 3D lighting
    if (useBevel) {
        vec3 normal = normalize(vec3(gradientX, gradientY, 1.0));
        vec3 lightDir = normalize(vec3(-1.0, -0.8, 0.5));
        
        float bevel = dot(normal, lightDir);
        float t = (bevel + 1.0) * 0.5;
        t = pow(t, 4.0);
        
        float topLeftBoost = smoothstep(0.5, 1.0, 1.0 - length(uv));
        t = min(1.0, t + topLeftBoost * 0.5);
        baseIntensity = mix(baseIntensity, t, bevelStrength * 1.5);
    }

    baseIntensity = clamp(baseIntensity, 0.0, 1.0);

    // Returning grayscale for debugging
    if (useGrayscale) {
        return vec3(baseIntensity);
    }

    vec3 finalColor;
    if (baseIntensity < 0.33) {
        float t = baseIntensity / 0.33;
        finalColor = mix(shadowColors[0], shadowColors[6], t);
    } else if (baseIntensity < 0.66) {
        float t = (baseIntensity - 0.33) / 0.33;
        finalColor = mix(midtoneColors[0], midtoneColors[7], t);
    } else {
        float t = (baseIntensity - 0.66) / 0.34;
        float highlightBoost = smoothstep(0.5, 1.0, 1.0 - length(uv - vec2(0.0, 1.0)));
        t = min(1.0, t + highlightBoost * 0.15);
        finalColor = mix(highlightColors[0], highlightColors[6], t);
    }
    // Final color adjustment
    finalColor = pow(finalColor, vec3(1.25));
    return finalColor;
}

// High-quality median blur implementation
vec4 preciseMedianBlur(vec2 uv) {
    vec2 texelSize = 1.0 / textureSize(text, 0);
    int sampleCount = 0;
    
    float radius = clamp(medianRadius * 0.9, 0.5, 2.5); 
    int iradius = int(ceil(radius));
    
    #define MAX_SAMPLES 64
    vec4 samples[MAX_SAMPLES];
    
    // Sample with proper edge handling
    for (int y = -iradius; y <= iradius; y++) {
        for (int x = -iradius; x <= iradius; x++) {
            vec2 sampleUV = uv + vec2(x,y) * texelSize;
            sampleUV = clamp(sampleUV, texelSize, vec2(1.0) - texelSize);
            
            vec3 color = calculateFinalColor(sampleUV);
            float alpha = texture(text, sampleUV).r;
            
            if (alpha >= 0.1 && sampleCount < MAX_SAMPLES) {
                samples[sampleCount++] = vec4(color, alpha);
            }
        }
    }
    
    if (sampleCount == 0) return vec4(0.0);
    
    // Sort samples by luminance to find median
    for (int i = 0; i < sampleCount-1; i++) {
        for (int j = i+1; j < sampleCount; j++) {
            float lumI = dot(samples[i].rgb, vec3(0.299, 0.587, 0.114));
            float lumJ = dot(samples[j].rgb, vec3(0.299, 0.587, 0.114));
            if (lumI > lumJ) {
                vec4 temp = samples[i];
                samples[i] = samples[j];
                samples[j] = temp;
            }
        }
    }
    
    int medianIndex = sampleCount / 2;
    return samples[medianIndex];
}

// Pixelization effect - creates blocky/pixelated look
vec4 applyPixelization(vec2 uv) {
    if (!pixelize) return vec4(calculateFinalColor(uv), texture(text, uv).r);
    
    // Calculate block-aligned coordinates
    vec2 texSize = vec2(textureSize(text, 0));
    vec2 pixelScale = pixelSize / texSize;
    
    // Calculate block-aligned coordinates
    vec2 blockCoord = floor(uv / pixelScale) * pixelScale;
    vec2 blockCenter = blockCoord + pixelScale * 0.5;
    
    // Sample center of each block (GIMP's method)
    vec3 color = calculateFinalColor(blockCenter);
    float alpha = texture(text, blockCenter).r;

    if (isShadow) {
        return vec4(color, alpha);
    }
    else {
        // Blend edges for smoother transitions
        vec2 fracPos = fract(uv / pixelScale);
        float edgeBlend = smoothstep(0.4, 0.6, max(abs(fracPos.x - 0.5), abs(fracPos.y - 0.5)));
    
        vec3 original = calculateFinalColor(uv);
        return vec4(mix(color, original, edgeBlend), alpha);
    }
}

void main() {
    float centerAlpha = texture(text, TexCoords).r;
    if (centerAlpha < 0.1) discard;

    // Special case for shadow rendering
    if (isShadow) {
        FragColor = applyPixelization(TexCoords);
    }
    
    // Only apply blur to interior (avoid edge distortion)
    vec2 texelSize = 1.0 / textureSize(text, 0);
    float edge = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float sample = texture(text, TexCoords + vec2(x,y) * texelSize).r;
            edge = max(edge, abs(sample - centerAlpha));
        }
    }
    edge = smoothstep(0.1, 0.3, edge);
    
    // Apply median blur to interior regions only
    vec4 blurred = preciseMedianBlur(TexCoords);
    vec3 original = calculateFinalColor(TexCoords);
    vec4 color = vec4(mix(blurred.rgb, original, edge), centerAlpha);
    
    // Final pixelization and output
    FragColor = applyPixelization(TexCoords);
    if (FragColor.a < 0.1) discard;
}
)";

GLuint CompileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    return shader;
}

GLuint CreateShaderProgram()
{
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    int success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

struct Character {
    GLuint TextureID;
    glm::ivec2 Size;
    glm::ivec2 Bearing;
    GLuint Advance;
};

std::map<GLchar, Character> Characters;
GLuint VAO, VBO, shaderProgram;

void LoadFont(const std::string& fontPath)
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
    {
        std::cerr << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
        return;
    }

    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face))
    {
        std::cerr << "ERROR::FREETYPE: Failed to load font at path: " << fontPath << std::endl;
        return;
    }

    FT_Set_Pixel_Sizes(face, 0, 48); // Font size

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Disable byte-alignment restriction

    // Load first 128 characters of ASCII set
    for (GLubyte c = 0; c < 128; c++)
    {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
        {
            std::cerr << "ERROR::FREETYPE: Failed to load Glyph for character: " << c << std::endl;
            continue;
        }

        // Generate texture
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );

        // Set texture options
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Store character for later use
        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<GLuint>(face->glyph->advance.x)
        };
        Characters.insert(std::pair<GLchar, Character>(c, character));
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void RenderText(const std::string& text, GLfloat x, GLfloat y, GLfloat scale, float interiorIntensity, float edgeIntensity, bool isShadow = false)
{
    glUseProgram(shaderProgram);
    glUniform1f(glGetUniformLocation(shaderProgram, "interiorIntensity"), interiorIntensity); // Set interior intensity
    glUniform1f(glGetUniformLocation(shaderProgram, "edgeIntensity"), edgeIntensity); // Set edge intensity
    glUniform1i(glGetUniformLocation(shaderProgram, "isShadow"), isShadow); // Enable highlight only for front letters
    glUniform1i(glGetUniformLocation(shaderProgram, "useBevel"), !isShadow);     // Enable bevel only for front letters
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(VAO);

    for (const char& c : text)
    {
        Character ch = Characters[c];

        GLfloat xpos = x + ch.Bearing.x * scale;
        GLfloat ypos = y - (ch.Size.y - ch.Bearing.y) * scale;

        // Apply shadow offset
        if (isShadow) {
            xpos -= -2 * scale; // Shadow offset to the left
            ypos -= 2 * scale;  // Shadow offset downwards
        }

        GLfloat w = ch.Size.x * scale;
        GLfloat h = ch.Size.y * scale;

        GLfloat vertices[6][4] = {
            { xpos,     ypos + h,   0.0, 0.0 }, // Bottom-left
            { xpos,     ypos,       0.0, 1.0 }, // Top-left
            { xpos + w, ypos,       1.0, 1.0 }, // Top-right

            { xpos,     ypos + h,   0.0, 0.0 }, // Bottom-left
            { xpos + w, ypos,       1.0, 1.0 }, // Top-right
            { xpos + w, ypos + h,   1.0, 0.0 }  // Bottom-right
        };

        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
        glUniform1i(glGetUniformLocation(shaderProgram, "text"), 0); // Set texture unit to 0

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += (ch.Advance >> 6) * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

int main()
{
    if (!glfwInit())
        return -1;

    GLFWwindow* window = glfwCreateWindow(800, 600, "Claw Text", NULL, NULL);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set up shaders
    shaderProgram = CreateShaderProgram();

    // Set up projection matrix
    glm::mat4 projection = glm::ortho(0.0f, 800.0f, 0.0f, 600.0f);
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    // Load the font
    LoadFont("../x64/Release/Text Fonts/BOOKOSB.ttf");

    // Set highlight uniforms
    glm::vec2 highlightCenter(0.0f, 0.0f);       // Top-left corner in texture coordinates
    float highlightRadius = 0.5f;                // Radius of the highlight effect
    float bevelStrength = 0.5f;                 // Strength of the bevel effect

    glUseProgram(shaderProgram);
    glUniform2fv(glGetUniformLocation(shaderProgram, "highlightCenter"), 1, glm::value_ptr(highlightCenter));
    glUniform1f(glGetUniformLocation(shaderProgram, "highlightRadius"), highlightRadius);
    glUniform1f(glGetUniformLocation(shaderProgram, "bevelStrength"), bevelStrength);

    while (!glfwWindowShouldClose(window))
    {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        RenderText("END GAME", 100.0f, 300.0f, 2.5f, 0.7f, 0.9f, true);
        RenderText("END GAME", 100.0f, 300.0f, 2.5f, 0.7f, 0.9f);        
        
        // Underline
        RenderText("_", 110.0f, 305.0f, 2.5f, 0.7f, 0.9f, true);
        RenderText("_", 110.0f, 305.0f, 2.5f, 0.7f, 0.9f);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}