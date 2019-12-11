/*
  ==============================================================================
Ross Hoyt
PluginEditor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WavefrontObjParser.h"
#include "Shape.h"
#include "Textures.h"
#include "Uniforms.h"
#include "Attributes.h"
#include "ShaderPreset.h"
#include "GLUtils.h"

//==============================================================================
class GLComponent : public Component, private OpenGLRenderer, public Slider::Listener
{
public:
    GLComponent(MidiKeyboardState &mKeybState, PluginEditor *par) :
    rotation (0.0f), scale (0.5f), rotationSpeed (0.1f),
    textureToUse (nullptr), lastTexture (nullptr)
    {
        parent = par;
        
        midiKeyboardState = &mKeybState;
        
        updateShader();
        
        lastTexture = textureToUse = new BuiltInTexture ("Portmeirion", BinaryData::portmeirion_jpg, BinaryData::portmeirion_jpgSize);
        
        openGLContext.setComponentPaintingEnabled (false);
        openGLContext.setContinuousRepainting (true);
        
        openGLContext.setRenderer (this);
        openGLContext.attachTo (*this);
    }
    
    ~GLComponent()
    {
        openGLContext.detach();
        openGLContext.setRenderer (nullptr);
        
        if (lastTexture != nullptr)
            delete lastTexture;
    }
    
    bool drawPianoKeys = true;
    bool drawControlMesh = true;
    
    String newVertexShader, newFragmentShader;
private:
    
    void newOpenGLContextCreated() override
    {
        freeAllContextObjects();
    }
    
    void renderOpenGL() override
    {
        jassert (OpenGLHelpers::isContextActive());
        
        const float desktopScale = (float) openGLContext.getRenderingScale();
        OpenGLHelpers::clear (Colours::cadetblue);
        
        updateShader();   // Check whether we need to compile a new shader
        
        if (shader == nullptr)
            return;
        
        glEnable (GL_DEPTH_TEST);
        glDepthFunc (GL_LESS);
        glEnable (GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        openGLContext.extensions.glActiveTexture (GL_TEXTURE0);
        glEnable (GL_TEXTURE_2D);
        
        glViewport (0, 0, roundToInt (desktopScale * getWidth()), roundToInt (desktopScale * getHeight()));
        texture.bind();
        
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        
        shader->use();
        
        if (uniforms->projectionMatrix != nullptr)
            uniforms->projectionMatrix->setMatrix4 (getProjectionMatrix().mat, 1, false);
        
        if (uniforms->texture != nullptr)
            uniforms->texture->set ((GLint) 0);
        
        if (uniforms->lightPosition != nullptr)
            uniforms->lightPosition->set (-15.0f, 10.0f, 15.0f, 0.0f);
        
        
        for(int i = 0; i < 128; i++)
        {
            
            if (uniforms->viewMatrix != nullptr)
                uniforms->viewMatrix->setMatrix4 (getViewMatrix(i).mat, 1, false);
            
            // TODO could add automatic MidiChannel Sensing
            if(drawPianoKeys)
            {
                if(drawControlMesh)
                    shapePianoKey->drawControlMesh(openGLContext, *attributes);
                if(midiKeyboardState->isNoteOn(1, i))
                    shapePianoKey->draw (openGLContext, *attributes);
            }
            else
            {
                if(drawControlMesh)
                    shapeTeapot->drawControlMesh(openGLContext, *attributes);
                if(midiKeyboardState->isNoteOn(1, i))
                    shapeTeapot->draw(openGLContext, *attributes);
            }
            
        }
        
        // Reset the element buffers so child Components draw correctly
        openGLContext.extensions.glBindBuffer (GL_ARRAY_BUFFER, 0);
        openGLContext.extensions.glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
        
        rotation += (float) rotationSpeed;
    }
    
    Matrix3D<float> getProjectionMatrix() const
    {
        auto w = 1.0f / (scale + 0.1f);
        auto h = w * getLocalBounds().toFloat().getAspectRatio (false);
        return Matrix3D<float>::fromFrustum (-w, w, -h, h, 4.0f, 30.0f);
    }
    
    Matrix3D<float> getViewMatrix(int i) const
    {
        int fundamentalPitch = i % 12;
        auto viewMatrix = draggableOrientation.getRotationMatrix()
        * Vector3D<float> (0.0f, 1.0f, -10.0f);
        
        float angle = 360.0f / 12.0f;
        float rotY = degreesToRadians(angle) * ((fundamentalPitch + 9) % 12);
        auto rotationMatrix = Matrix3D<float>::rotation ({ 1.55f, rotY, -1.55f });
        // TODO Control Cylinder Radius with Slider
        float cylinderRadius = 2.0f;
        
        float translateY = i * -.5f + 30.0f;
        float translateX = cylinderRadius * sin(degreesToRadians(fundamentalPitch * angle));
        float translateZ = cylinderRadius * cos(degreesToRadians(fundamentalPitch * angle));
        auto translationVector = Matrix3D<float>(Vector3D<float> ({ translateX, translateY, translateZ}));
        
        return  rotationMatrix * translationVector * viewMatrix;
        
    }
    void openGLContextClosing() override
    {
        freeAllContextObjects();
        
        if (lastTexture != nullptr)
            setTexture (lastTexture);
    }
    
    void paint(Graphics& g) override {}
    
    void setTexture (DemoTexture* t)
    {
        lastTexture = textureToUse = t;
    }
    
    void freeAllContextObjects()
    {
        shapePianoKey = nullptr;
        shader = nullptr;
        attributes = nullptr;
        uniforms = nullptr;
        texture.release();
    }
    
    void resized() override
    {
        draggableOrientation.setViewport (getLocalBounds());
    }
    
    Draggable3DOrientation draggableOrientation;
    float rotation;
    float scale, rotationSpeed;
    
    ScopedPointer<OpenGLShaderProgram> shader;
    ScopedPointer<Shape> shapePianoKey;
    ScopedPointer<Shape> shapeTeapot;
    
    ScopedPointer<Attributes> attributes;
    ScopedPointer<Uniforms> uniforms;
    
    OpenGLTexture texture;
    DemoTexture* textureToUse, *lastTexture;
    
    OpenGLContext openGLContext;
    
    //==============================================================================
    
    
    void updateShader()
    {
        if (newVertexShader.isNotEmpty() || newFragmentShader.isNotEmpty())
        {
            ScopedPointer<OpenGLShaderProgram> newShader (new OpenGLShaderProgram (openGLContext));
            String statusText;
            
            if (newShader->addVertexShader (OpenGLHelpers::translateVertexShaderToV3 (newVertexShader))
                && newShader->addFragmentShader (OpenGLHelpers::translateFragmentShaderToV3 (newFragmentShader))
                && newShader->link())
            {
                shapePianoKey = nullptr;
                shapeTeapot = nullptr;
                attributes = nullptr;
                uniforms = nullptr;
                
                shader = newShader;
                shader->use();
                
                shapePianoKey = new Shape (openGLContext, BinaryData::pianokey_rectangle_obj);
                shapeTeapot   = new Shape (openGLContext, BinaryData::teapot_obj);
                attributes = new Attributes (openGLContext, *shader);
                uniforms   = new Uniforms (openGLContext, *shader);
                
                statusText = "GLSL: v" + String (OpenGLShaderProgram::getLanguageVersion(), 2);
            }
            else
            {
                statusText = newShader->getLastError();
            }
            
            newVertexShader = String();
            newFragmentShader = String();
        }
    }
    
    //==============================================================================
    // INPUT HANDLING
    
    void sliderValueChanged (Slider*) override
    {
        scale = (float) parent->zoomSlider.getValue();
    }
    
    void mouseDown (const MouseEvent& e) override
    {
        draggableOrientation.mouseDown (e.getPosition());
    }
    
    void mouseDrag (const MouseEvent& e) override
    {
        draggableOrientation.mouseDrag (e.getPosition());
    }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& d) override
    {
        parent->zoomSlider.setValue(parent->zoomSlider.getValue() + d.deltaY);
    }
    MidiKeyboardState * midiKeyboardState;
    PluginEditor *parent;
};

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p, MidiKeyboardState& midiKeyboardState)
: AudioProcessorEditor (&p), processor (p), midiKeyboardComponent(midiKeyboardState, MidiKeyboardComponent::horizontalKeyboard)
{
    // TOP: MIDI KEYBOARD DISPLAY
    addAndMakeVisible(midiKeyboardComponent);
    
    // LEFT TOOLBAR: OBJ FILE SELECTOR (RADIO BOX GROUP)
    radioButtonsObjSelector = new GroupComponent ("OBJ Selector", "Use Obj File:");
    addAndMakeVisible (radioButtonsObjSelector);
    togglePianoKeyObj = new ToggleButton("Pianokey_rectangle.obj");
    toggleTeapotObj   = new ToggleButton("Teapot.obj");
    togglePianoKeyObj->setRadioGroupId(ObjSelectorButtons);
    toggleTeapotObj  ->setRadioGroupId(ObjSelectorButtons);
    togglePianoKeyObj->onClick = [this] { updateToggleState (togglePianoKeyObj,   "Pianokey_rectangle.obj");   };
    toggleTeapotObj  ->onClick = [this] { updateToggleState (toggleTeapotObj, "Teapot.obj"); };
    addAndMakeVisible(togglePianoKeyObj);
    addAndMakeVisible(toggleTeapotObj);
    
    // LEFT TOOLBAR: ZOOM SLIDER
    addAndMakeVisible (zoomSlider);
    zoomSlider.setRange (0.0, 1.0, 0.001);
    zoomSlider.addListener (glComponent);
    zoomSlider.setSliderStyle (Slider::LinearVertical);
    zoomLabel.attachToComponent (&zoomSlider, false);
    addAndMakeVisible (zoomLabel);
    
    // LEFT RADIO BOX: 'Draw Points'
    toggleDrawControlMesh = new ToggleButton("Draw Points");
    toggleDrawControlMesh->onClick = [this] { updateToggleState (toggleDrawControlMesh, "Draw Points");   };
    addAndMakeVisible(toggleDrawControlMesh);
    
    
    addAndMakeVisible (shaderPresetBox);
    shaderPresetBox.onChange = [this] { selectShaderPreset (shaderPresetBox.getSelectedItemIndex()); };
    
    shaders = getShaderPresets(SHADERS_ABS_DIR_PATH);
    
    for (int i = 0; i < shaders.size(); ++i)
        shaderPresetBox.addItem (shaders[i].name, i + 1);
    
    shaderPresetBoxLabel.attachToComponent (&shaderPresetBox, true);
    addAndMakeVisible(shaderPresetBoxLabel);
    addAndMakeVisible (shaderPresetBoxLabel);
    
    // BOTTOM RIGHT - OPENGL DISPLAY
    glComponent = new GLComponent(midiKeyboardState, this);
    addAndMakeVisible (glComponent);
    
    
    
    initialise();
    
  
    
    
    //setResizable(true, true);
    setSize (1000, 725);
}

PluginEditor::~PluginEditor()
{
    glComponent = nullptr;
}

void PluginEditor::initialise()
{
    togglePianoKeyObj->setToggleState(true, false);
    toggleDrawControlMesh->setToggleState(true, false);
    zoomSlider .setValue (0.5);
    
}


void PluginEditor::updateToggleState (Button* button, String name)
{
    if(name == "Draw Points") glComponent->drawControlMesh = button->getToggleState();
    else if(button->getToggleState())
    {
        if(name == "Pianokey_rectangle.obj") glComponent->drawPianoKeys = true;
        else if(name == "Teapot.obj") glComponent->drawPianoKeys = false;
    }
}

void PluginEditor::selectShaderPreset (int preset)
{
    const auto& p = shaders[preset];
    setShaderProgram(p.vertexShader, p.fragmentShader);
}

void PluginEditor::setShaderProgram (const String& vertexShader, const String& fragmentShader)
{
    glComponent->newVertexShader = vertexShader;
    glComponent->newFragmentShader = fragmentShader;
}
//==============================================================================

//==============================================================================
void PluginEditor::paint (Graphics& g)
{
    g.fillAll(backgroundColor);
}

Rectangle<int> PluginEditor::getSubdividedRegion(Rectangle<int> region, int numer, int denom, SubdividedOrientation orientation)
{
    int x, y, height, width;
    if(orientation == Vertical)
    {
        x = region.getX();
        width = region.getWidth();
        height = region.getHeight() / denom;
        y = region.getY() + numer * height;
    }
    else
    {
        y = region.getY();
        height = region.getHeight();
        width = region.getWidth() / denom;
        x = region.getX() + numer * width;
    }
    return Rectangle<int>(x, y, width, height);
}

void PluginEditor::resized()
{
    Rectangle<int> r = getLocalBounds();
    float resizedKeybWidth = r.getWidth() - MARGIN * 2, resizedKeybHeight = r.getHeight() - 5;
    float keybWidth = resizedKeybWidth > MAX_KEYB_WIDTH ? MAX_KEYB_WIDTH : resizedKeybWidth;
    float keybHeight = resizedKeybHeight > MAX_KEYB_HEIGHT ? MAX_KEYB_HEIGHT : resizedKeybHeight;
    midiKeyboardComponent.setBounds (MARGIN, MARGIN, keybWidth, keybHeight );
    
    auto areaBelowKeyboard = r.removeFromBottom(r.getHeight() - (keybHeight + MARGIN));
    
    int leftToolbarWidth = r.getWidth() / 7;
    
    auto glArea = areaBelowKeyboard.removeFromRight(r.getWidth() - leftToolbarWidth);
    glComponent->setBounds(glArea);
    
    auto leftButtonToolbarArea = areaBelowKeyboard.removeFromLeft(glArea.getWidth());
    auto radioObjSelectorRegion = leftButtonToolbarArea.removeFromTop((BUTTON_HEIGHT * 2 + MARGIN*3));
    radioObjSelectorRegion.translate(0, MARGIN);
    radioButtonsObjSelector->setBounds (radioObjSelectorRegion);//MARGIN, keybHeight + MARGIN, 220, 140);
    togglePianoKeyObj->setBounds(getSubdividedRegion(radioObjSelectorRegion, 1, 3, Vertical));//
    toggleTeapotObj->setBounds (getSubdividedRegion(radioObjSelectorRegion, 2, 3, Vertical));
    
    int sliderRegionHeight = leftButtonToolbarArea.getHeight() / 4 + BUTTON_HEIGHT;
    auto sliderRegion1 = leftButtonToolbarArea.removeFromTop(sliderRegionHeight);
    zoomSlider.setBounds(sliderRegion1.removeFromBottom(sliderRegion1.getHeight() - BUTTON_HEIGHT));
    zoomSlider.setTextBoxStyle (Slider::TextBoxBelow, false, BUTTON_WIDTH, 20);
    
    toggleDrawControlMesh->setBounds(leftButtonToolbarArea.removeFromTop(radioObjSelectorRegion.getHeight()));
    
    shaderPresetBox.setBounds(leftButtonToolbarArea.removeFromTop(BUTTON_HEIGHT * 3));
    
}
